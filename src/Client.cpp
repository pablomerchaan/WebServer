#include "../inc/Client.hpp"
#include "../inc/RequestHandler.hpp"
#include <sys/socket.h>
#include <unistd.h>

static const std::time_t CLIENT_TIMEOUT = 60; // segundos sin actividad antes de cerrar
static const std::time_t CGI_TIMEOUT = 10;    // segundos máximos de ejecución de un CGI
static const size_t SEND_CHUNK = 65536;       // bytes máximos por send()

Client::Client(int fd, const std::string& address, int port,
			   const std::vector<const Server*>& servers)
	: _fd(fd), _address(address), _port(port), _servers(servers), _server(servers[0]),
	  _state(READING), _write_offset(0), _keep_alive(true), _cgi(NULL),
	  _last_activity(std::time(NULL))
{
}

Client::~Client()
{
	delete _cgi;  // el destructor de Cgi cierra pipes y mata/recoge el proceso
	close(_fd);
}

int Client::getFd() const { return _fd; }
Client::State Client::getState() const { return _state; }
Cgi* Client::getCgi() const { return _cgi; }

/* ======================================================================
 *  Lectura y escritura del socket
 * ====================================================================== */

bool Client::onReadable()
{
	char buffer[65536];
	ssize_t bytes = recv(_fd, buffer, sizeof(buffer), 0);
	// 0 = el cliente cerró la conexión; -1 = error. En ambos casos cerramos.
	// (El subject prohíbe mirar errno tras recv/send para decidir qué hacer.)
	if (bytes <= 0)
		return false;
	_last_activity = std::time(NULL);
	_request.append(buffer, bytes);
	processInput();
	return true;
}

bool Client::onWritable()
{
	// Enviamos como mucho SEND_CHUNK bytes; send() puede enviar menos de lo
	// pedido (el buffer del socket está lleno), así que llevamos la cuenta
	// en _write_offset y continuamos en el siguiente POLLOUT
	size_t remaining = _write_buffer.size() - _write_offset;
	size_t length = remaining < SEND_CHUNK ? remaining : SEND_CHUNK;
	ssize_t sent = send(_fd, _write_buffer.data() + _write_offset, length, 0);
	if (sent <= 0)
		return false;
	_last_activity = std::time(NULL);
	_write_offset += sent;
	if (_write_offset < _write_buffer.size())
		return true;  // queda respuesta por enviar

	// Respuesta enviada entera
	if (!_keep_alive)
		return false;
	// Keep-alive: preparamos la conexión para la siguiente petición
	_write_buffer.clear();
	_write_offset = 0;
	_request.reset();
	_server = _servers[0];
	_state = READING;
	// Si el cliente ya mandó la siguiente petición (pipelining), la
	// procesamos ya: esos bytes están en el buffer y poll() no volverá a
	// avisarnos de ellos porque ya los leímos del socket
	if (_request.hasBufferedData())
		processInput();
	return true;
}

/* ======================================================================
 *  Procesado de la petición
 * ====================================================================== */

void Client::processInput()
{
	HttpRequest::State state = _request.parse();
	if (state == HttpRequest::HEADERS_DONE)
	{
		// Ya tenemos Host y URI: elegimos el server (virtual host) y le
		// decimos al parser cuál es el tamaño máximo del body
		_server = &RequestHandler::selectServer(_servers, _request);
		_request.setMaxBodySize(RequestHandler::maxBodySize(*_server, _request));
		state = _request.getState();
	}
	if (state == HttpRequest::ERROR)
		queueError(_request.getErrorCode());
	else if (state == HttpRequest::COMPLETE)
		dispatch();
	// En cualquier otro estado faltan datos: esperamos al siguiente recv()
}

void Client::dispatch()
{
	RequestHandler handler(*_server, _request, _address, _port);
	Cgi* cgi = NULL;
	HttpResponse response = handler.handle(cgi);
	if (cgi != NULL)
	{
		// Se ha lanzado un CGI: la respuesta la construiremos cuando acabe
		_cgi = cgi;
		_state = CGI;
		return;
	}
	queueResponse(response, _request.keepAlive());
}

// Serializa la respuesta y pasa a WRITING (poll empezará a vigilar POLLOUT)
void Client::queueResponse(const HttpResponse& response, bool keep_alive)
{
	_keep_alive = keep_alive;
	_write_buffer = response.build(keep_alive, _request.getMethod() != "HEAD");
	_write_offset = 0;
	_state = WRITING;
}

void Client::queueError(int code)
{
	// Tras un error de protocolo no sabemos dónde empieza la siguiente
	// petición en el flujo de bytes: respondemos y cerramos la conexión
	queueResponse(RequestHandler::errorResponse(*_server, code), false);
}

/* ======================================================================
 *  CGI
 * ====================================================================== */

void Client::onCgiWritable()
{
	if (_cgi != NULL)
		_cgi->writeInput();
}

void Client::onCgiReadable()
{
	// readOutput() devuelve false en EOF: el script ha terminado de escribir
	if (_cgi != NULL && !_cgi->readOutput())
		finishCgi(0);
}

/*
 * Termina el CGI y prepara la respuesta. error_code:
 *   0   -> terminó normalmente: respuesta construida a partir de su salida
 *   504 -> timeout: se mata el script y se responde Gateway Timeout
 */
void Client::finishCgi(int error_code)
{
	HttpResponse response;
	if (error_code == 0)
		response = _cgi->buildResponse();
	delete _cgi;  // cierra pipes y recoge (o mata) el proceso
	_cgi = NULL;
	_last_activity = std::time(NULL);
	// 502 = salida del script inválida. Usamos errorResponse para que se
	// apliquen también las páginas de error personalizadas
	if (error_code != 0 || response.getStatus() == 502)
		queueResponse(RequestHandler::errorResponse(*_server, error_code ? error_code : 502),
					  _request.keepAlive());
	else
		queueResponse(response, _request.keepAlive());
}

/*
 * Se llama una vez por vuelta del bucle principal. Es lo que garantiza que
 * el servidor nunca se quede esperando para siempre:
 *  - CGI que tarda demasiado (bucle infinito) -> se mata y se responde 504
 *  - cliente inactivo -> si dejó una petición a medias se le manda 408 Request
 *    Timeout; si no (conexión keep-alive ociosa) simplemente se cierra
 */
bool Client::checkTimeout(std::time_t now)
{
	if (_state == CGI)
	{
		if (now - _cgi->getStartTime() > CGI_TIMEOUT)
			finishCgi(504);
		return true;
	}
	if (now - _last_activity <= CLIENT_TIMEOUT)
		return true;
	// Petición a medias: hay bytes sin procesar, o el parser ya pasó de las
	// cabeceras (el body recibido está en la petición, no en el buffer)
	if (_state == READING &&
		(_request.hasBufferedData() || _request.getState() != HttpRequest::HEADERS))
	{
		_last_activity = now;  // darle tiempo a recibir el 408
		queueError(408);
		return true;
	}
	return false;  // cerrar
}
