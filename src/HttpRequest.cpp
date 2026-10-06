#include "../inc/HttpRequest.hpp"
#include "../inc/Utils.hpp"
#include <cstdlib>

/*
 * Límites de seguridad: sin ellos, un cliente podría mandarnos cabeceras
 * infinitas y hacernos consumir toda la memoria.
 */
static const size_t MAX_HEADER_SIZE = 16384;  // línea de petición + cabeceras
static const size_t MAX_URI_SIZE = 8192;      // longitud de la URI

HttpRequest::HttpRequest()
	: _state(HEADERS), _content_length(0), _chunk_remaining(0), _max_body_size(0),
	  _error_code(0)
{
}

/*
 * Con keep-alive el cliente puede haber enviado ya la siguiente petición
 * (pipelining). Esos bytes están en _buffer: los guardamos, reiniciamos todo
 * lo demás y los volvemos a poner.
 */
void HttpRequest::reset()
{
	std::string leftover = _buffer;
	*this = HttpRequest();
	_buffer = leftover;
}

void HttpRequest::append(const char* data, size_t size)
{
	_buffer.append(data, size);
}

HttpRequest::State HttpRequest::fail(int code)
{
	_error_code = code;
	_state = ERROR;
	return _state;
}

bool HttpRequest::reject(int code)
{
	fail(code);
	return false;
}

/* ======================================================================
 *  Bucle principal del parser
 * ====================================================================== */

HttpRequest::State HttpRequest::parse()
{
	if (_state == HEADERS)
	{
		// Las cabeceras terminan con una línea vacía: "\r\n\r\n"
		size_t end = _buffer.find("\r\n\r\n");
		size_t separator = 4;
		if (end == std::string::npos)
		{
			// Somos tolerantes con clientes que solo usan '\n' (p.ej. telnet/nc)
			end = _buffer.find("\n\n");
			separator = 2;
		}
		if (end == std::string::npos)
		{
			// Aún no han llegado todas las cabeceras. Si ya llevamos demasiado,
			// cortamos: 414 si ni siquiera ha acabado la primera línea (URI
			// gigante), 431 si son las cabeceras las que son enormes.
			if (_buffer.size() > MAX_HEADER_SIZE)
				return fail(_buffer.find('\n') == std::string::npos ? 414 : 431);
			return _state;  // esperar más datos
		}
		if (end > MAX_HEADER_SIZE)
			return fail(431);
		// Sacamos la sección de cabeceras del buffer; lo que queda es body
		// (o la siguiente petición)
		std::string section = _buffer.substr(0, end);
		_buffer.erase(0, end + separator);
		if (!parseHeaderSection(section))
			return _state;  // parseHeaderSection ya puso ERROR
		// Pausa: el Client llamará a setMaxBodySize() para continuar
		_state = HEADERS_DONE;
	}
	if (_state == BODY)
		parseBody();
	if (_state == CHUNK_SIZE || _state == CHUNK_DATA || _state == CHUNK_TRAILER)
		parseChunks();
	return _state;
}

/* ======================================================================
 *  Cabeceras
 * ====================================================================== */

/*
 * "section" contiene algo como:
 *     POST /upload HTTP/1.1\r\n
 *     Host: localhost:8002\r\n
 *     Content-Length: 5
 */
bool HttpRequest::parseHeaderSection(const std::string& section)
{
	// Se permiten líneas vacías antes de la línea de petición (RFC 9112 2.2)
	size_t start = section.find_first_not_of("\r\n");
	if (start == std::string::npos)
		return reject(400);

	// --- Primera línea: "MÉTODO URI VERSIÓN" ---
	size_t line_end = section.find('\n', start);
	std::string request_line = section.substr(start, line_end == std::string::npos
														 ? std::string::npos : line_end - start);
	if (!request_line.empty() && request_line[request_line.size() - 1] == '\r')
		request_line.erase(request_line.size() - 1);
	if (!parseRequestLine(request_line))
		return false;

	// --- Resto de líneas: "Nombre: valor" ---
	while (line_end != std::string::npos)
	{
		size_t next = section.find('\n', line_end + 1);
		std::string line = section.substr(line_end + 1, next == std::string::npos
															? std::string::npos : next - line_end - 1);
		line_end = next;
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);  // quitamos el '\r' del "\r\n"
		if (line.empty())
			continue;
		size_t colon = line.find(':');
		if (colon == std::string::npos || colon == 0)
			return reject(400);  // línea sin ':' o sin nombre
		// Los nombres de cabecera no distinguen mayúsculas: los guardamos en
		// minúscula para poder buscarlos siempre igual ("content-length")
		std::string name = toLower(line.substr(0, colon));
		if (name.find_first_of(" \t") != std::string::npos)
			return reject(400);  // "Host :" no es válido (RFC 9112 5.1)
		std::string value = trim(line.substr(colon + 1));
		if (_headers.count(name))
		{
			// Host o Content-Length repetidos con valores distintos son un
			// ataque clásico (request smuggling): lo rechazamos
			if (name == "host" || name == "content-length")
			{
				if (_headers[name] != value)
					return reject(400);
			}
			else
				_headers[name] += ", " + value;  // el resto se combinan con comas
		}
		else
			_headers[name] = value;
	}

	// HTTP/1.1 obliga a enviar Host
	if (_version == "HTTP/1.1" && !_headers.count("host"))
		return reject(400);

	// ¿Cómo sabremos dónde acaba el body?
	if (_headers.count("transfer-encoding"))
	{
		// Solo implementamos "chunked"
		if (toLower(_headers["transfer-encoding"]) != "chunked")
			return reject(501);
		// Las dos cabeceras a la vez son ambiguas: rechazamos
		if (_headers.count("content-length"))
			return reject(400);
	}
	else if (_headers.count("content-length"))
	{
		// Solo dígitos, y no más de 18 para que no desborde un size_t
		const std::string& value = _headers["content-length"];
		if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos ||
			value.size() > 18)
			return reject(400);
		_content_length = std::strtoul(value.c_str(), NULL, 10);
	}
	return true;
}

// "GET /index.html HTTP/1.1" -> método, URI y versión
bool HttpRequest::parseRequestLine(const std::string& line)
{
	// Exactamente tres partes separadas por espacios
	size_t first = line.find(' ');
	size_t last = line.rfind(' ');
	if (first == std::string::npos || first == last)
		return reject(400);
	_method = line.substr(0, first);
	_target = line.substr(first + 1, last - first - 1);
	_version = line.substr(last + 1);

	// El método es una palabra en mayúsculas. Que no lo IMPLEMENTEMOS se
	// decide más tarde (501) en el RequestHandler.
	if (_method.empty() || _method.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos)
		return reject(400);
	if (_target.empty() || _target.find(' ') != std::string::npos)
		return reject(400);
	if (_target.size() > MAX_URI_SIZE)
		return reject(414);
	if (_version.compare(0, 5, "HTTP/") != 0)
		return reject(400);
	// Solo hablamos HTTP/1.0 y 1.1
	if (_version != "HTTP/1.1" && _version != "HTTP/1.0")
		return reject(505);
	return parseTarget();
}

/*
 * Separa la URI en ruta y query y la deja lista para usar:
 *   "/a%20b/../c.py?x=1#frag" -> _path "/c.py", _query "x=1"
 */
bool HttpRequest::parseTarget()
{
	std::string target = _target;
	// Forma absoluta (la usan los proxies): "http://host:puerto/ruta" -> "/ruta"
	if (target.compare(0, 7, "http://") == 0)
	{
		size_t slash = target.find('/', 7);
		target = slash == std::string::npos ? "/" : target.substr(slash);
	}
	if (target[0] != '/')
		return reject(400);
	// El fragmento (#...) nunca debería llegar al servidor; si llega, se ignora
	size_t hash = target.find('#');
	if (hash != std::string::npos)
		target.erase(hash);
	// La query string se guarda tal cual (codificada): es para el CGI
	size_t question = target.find('?');
	if (question != std::string::npos)
	{
		_query = target.substr(question + 1);
		target.erase(question);
	}
	// Decodificamos %XX y resolvemos "."/"..": si la ruta intenta escapar de
	// la raíz ("/../../etc/passwd") es un 400
	std::string decoded;
	if (!urlDecode(target, decoded) || !normalizePath(decoded, _path))
		return reject(400);
	return true;
}

/* ======================================================================
 *  Body
 * ====================================================================== */

void HttpRequest::setMaxBodySize(unsigned long size)
{
	if (_state != HEADERS_DONE)
		return;
	_max_body_size = size;
	if (_headers.count("transfer-encoding"))
		_state = CHUNK_SIZE;  // el tamaño se irá comprobando chunk a chunk
	else if (_content_length > 0)
	{
		// Con Content-Length sabemos el tamaño de antemano: rechazamos ya,
		// sin esperar a recibir el body
		if (_max_body_size != 0 && _content_length > _max_body_size)
		{
			fail(413);
			return;
		}
		_state = BODY;
	}
	else
		_state = COMPLETE;  // sin body
	// Puede que parte (o todo) el body ya estuviera en el buffer
	parse();
}

// Body con Content-Length: copiar bytes hasta tener exactamente esa cantidad
void HttpRequest::parseBody()
{
	size_t needed = _content_length - _body.size();
	size_t take = needed < _buffer.size() ? needed : _buffer.size();
	_body.append(_buffer, 0, take);
	_buffer.erase(0, take);  // lo que sobre es la siguiente petición
	if (_body.size() == _content_length)
		_state = COMPLETE;
}

/*
 * Transfer-Encoding: chunked. El body llega en trozos, cada uno precedido de
 * su tamaño en hexadecimal, y termina con un trozo de tamaño 0:
 *
 *     5\r\n          <- tamaño (hex)
 *     hola \r\n      <- 5 bytes de datos + \r\n
 *     4\r\n
 *     mund\r\n
 *     0\r\n          <- fin
 *     \r\n           <- (trailers opcionales) + línea vacía
 *
 * Vamos juntando solo los datos en _body ("hola mund"): el subject pide que
 * el CGI reciba el body ya "des-chunkeado".
 */
void HttpRequest::parseChunks()
{
	while (true)
	{
		if (_state == CHUNK_SIZE)
		{
			size_t end = _buffer.find("\r\n");
			if (end == std::string::npos)
			{
				// Una línea de tamaño no puede ser tan larga: cliente malicioso
				if (_buffer.size() > 1024)
					fail(400);
				return;  // esperar más datos
			}
			std::string line = _buffer.substr(0, end);
			_buffer.erase(0, end + 2);
			// Pueden venir "extensiones" tras ';' ("5;name=val"): se ignoran
			size_t semicolon = line.find(';');
			if (semicolon != std::string::npos)
				line.erase(semicolon);
			line = trim(line);
			if (line.empty() || line.size() > 15 ||
				line.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
			{
				fail(400);
				return;
			}
			_chunk_remaining = std::strtoul(line.c_str(), NULL, 16);
			if (_chunk_remaining == 0)
				_state = CHUNK_TRAILER;  // último chunk
			else if (_max_body_size != 0 && _body.size() + _chunk_remaining > _max_body_size)
			{
				fail(413);  // el body acumulado superaría el límite
				return;
			}
			else
				_state = CHUNK_DATA;
		}
		else if (_state == CHUNK_DATA)
		{
			// Esperamos a tener el chunk completo más su "\r\n" final
			if (_buffer.size() < _chunk_remaining + 2)
				return;
			if (_buffer.compare(_chunk_remaining, 2, "\r\n") != 0)
			{
				fail(400);  // el tamaño anunciado no coincidía con los datos
				return;
			}
			_body.append(_buffer, 0, _chunk_remaining);
			_buffer.erase(0, _chunk_remaining + 2);
			_state = CHUNK_SIZE;  // a por el siguiente chunk
		}
		else if (_state == CHUNK_TRAILER)
		{
			// Tras el chunk 0 pueden venir cabeceras "trailer" (las ignoramos)
			// hasta una línea vacía, que marca el final de la petición
			size_t end = _buffer.find("\r\n");
			if (end == std::string::npos)
				return;
			_buffer.erase(0, end + 2);
			if (end == 0)
			{
				_state = COMPLETE;
				return;
			}
		}
		else
			return;
	}
}

/* ======================================================================
 *  Getters
 * ====================================================================== */

HttpRequest::State HttpRequest::getState() const { return _state; }
int HttpRequest::getErrorCode() const { return _error_code; }
const std::string& HttpRequest::getMethod() const { return _method; }
const std::string& HttpRequest::getTarget() const { return _target; }
const std::string& HttpRequest::getPath() const { return _path; }
const std::string& HttpRequest::getQuery() const { return _query; }
const std::string& HttpRequest::getVersion() const { return _version; }
const std::map<std::string, std::string>& HttpRequest::getHeaders() const { return _headers; }
const std::string& HttpRequest::getBody() const { return _body; }
bool HttpRequest::hasBufferedData() const { return !_buffer.empty(); }

std::string HttpRequest::getHeader(const std::string& name) const
{
	std::map<std::string, std::string>::const_iterator it = _headers.find(toLower(name));
	return it == _headers.end() ? "" : it->second;
}

/*
 * HTTP/1.1: la conexión se mantiene abierta salvo "Connection: close".
 * HTTP/1.0: se cierra salvo que pidan explícitamente "Connection: keep-alive".
 */
bool HttpRequest::keepAlive() const
{
	std::string connection = toLower(getHeader("connection"));
	if (_version == "HTTP/1.0")
		return connection == "keep-alive";
	return connection != "close";
}
