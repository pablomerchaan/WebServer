#include "../inc/RequestHandler.hpp"
#include "../inc/Utils.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <unistd.h>

RequestHandler::RequestHandler(const Server& server, const HttpRequest& request,
							   const std::string& client_address, int port)
	: _server(server), _request(request), _client_address(client_address), _port(port)
{
	// Si ninguna location coincide con la URI usamos una vacía: hereda todo
	// del server (root, index) y permite todos los métodos
	_fallback.setPath("/");
	_location = _server.matchLocation(_request.getPath());
	if (_location == NULL)
		_location = &_fallback;
}

/* ======================================================================
 *  Funciones estáticas (se usan antes de tener la petición completa)
 * ====================================================================== */

/*
 * Virtual hosts: dos bloques "server" pueden escuchar en el mismo puerto.
 * El navegador manda "Host: example.local:8003"; quitamos el puerto y
 * buscamos un server con ese server_name. Si no hay, el primero es el de
 * por defecto (igual que nginx).
 */
const Server& RequestHandler::selectServer(const std::vector<const Server*>& servers,
										   const HttpRequest& request)
{
	std::string host = toLower(request.getHeader("host"));
	size_t colon = host.find(':');
	if (colon != std::string::npos)
		host.erase(colon);
	for (size_t i = 0; i < servers.size(); ++i)
		if (servers[i]->hasServerName(host))
			return *servers[i];
	return *servers[0];
}

// El límite de la location (si lo tiene) manda sobre el del server
unsigned long RequestHandler::maxBodySize(const Server& server, const HttpRequest& request)
{
	const Location* location = server.matchLocation(request.getPath());
	if (location != NULL && location->hasClientMaxBodySize())
		return location->getClientMaxBodySize();
	return server.getClientMaxBodySize();
}

/*
 * Si la config tiene "error_page <code> <ruta>", servimos ese fichero
 * (ruta relativa al root del server). Si no existe o no hay, usamos la
 * página generada por defecto. El código de estado es el del error en
 * ambos casos (un 404 personalizado sigue siendo un 404).
 */
HttpResponse RequestHandler::errorResponse(const Server& server, int code)
{
	HttpResponse response;
	response.setStatus(code);
	std::map<int, std::string>::const_iterator it = server.getErrorPages().find(code);
	std::string content;
	if (it != server.getErrorPages().end() &&
		readFile(joinPath(server.getRoot(), it->second), content))
		response.setBody(content, mimeType(it->second));
	else
		response.setBody(defaultErrorPage(code), "text/html");
	return response;
}

/* ======================================================================
 *  Ayudantes
 * ====================================================================== */

/*
 * Traduce una URI a una ruta del disco. Dos modos:
 *
 *  root (como nginx): se añade la URI COMPLETA a la raíz
 *      root www;      "/tours/a.html"  -> "www/tours/a.html"
 *
 *  alias: se SUSTITUYE el prefijo de la location (ejemplo del subject:
 *  "/kapouet" enraizado en "/tmp/www")
 *      location /kapouet { alias www/kapouet; }
 *      "/kapouet/pouic/toto" -> "www/kapouet" + "/pouic/toto"
 */
std::string RequestHandler::mapPath(const std::string& uri_path) const
{
	if (!_location->getAlias().empty())
		return joinPath(_location->getAlias(), uri_path.substr(_location->getPath().size()));
	const std::string& root = _location->getRoot().empty() ? _server.getRoot()
														   : _location->getRoot();
	return joinPath(root.empty() ? "." : root, uri_path);
}

std::string RequestHandler::indexName() const
{
	return _location->getIndex().empty() ? _server.getIndex() : _location->getIndex();
}

/*
 * Una URI CGI puede llevar información extra detrás del script:
 *     /cgi-bin/hello.sh/extra/path
 *     \______________/\_________/
 *        script_uri     PATH_INFO
 * Probamos cada corte en un '/' (de más corto a más largo) hasta encontrar
 * un fichero que exista y cuya extensión esté en cgi_ext.
 */
bool RequestHandler::findCgiScript(const std::string& uri_path, std::string& script_uri,
								   std::string& path_info) const
{
	if (_location->getCgiExtensions().empty())
		return false;  // esta location no tiene CGI
	for (size_t end = 1; end <= uri_path.size(); ++end)
	{
		if (end != uri_path.size() && uri_path[end] != '/')
			continue;  // solo cortamos en límites de segmento
		std::string candidate = uri_path.substr(0, end);
		std::string interpreter;
		if (_location->getCgiInterpreter(getExtension(candidate), interpreter) &&
			isRegularFile(mapPath(candidate)))
		{
			script_uri = candidate;
			path_info = uri_path.substr(end);
			return true;
		}
	}
	return false;
}

// Respuesta 30x: el navegador lee la cabecera Location y va allí
HttpResponse RequestHandler::redirect(int code, const std::string& location)
{
	HttpResponse response;
	response.setStatus(code);
	response.setHeader("Location", location);
	// Un pequeño cuerpo con enlace, por si el cliente no sigue redirecciones
	response.setBody("<html><body><a href=\"" + htmlEscape(location) + "\">" +
						 toString(code) + " " + statusReason(code) + "</a></body></html>\n",
					 "text/html");
	return response;
}

/* ======================================================================
 *  Punto de entrada: decidir qué hacer con la petición
 * ====================================================================== */

HttpResponse RequestHandler::handle(Cgi*& cgi)
{
	cgi = NULL;
	const std::string& method = _request.getMethod();

	// 1) Solo implementamos estos métodos (el subject pide GET, POST y DELETE)
	if (method != "GET" && method != "HEAD" && method != "POST" && method != "DELETE")
		return errorResponse(_server, 501);

	// 2) ¿Lo permite la location? (HEAD va implícito con GET, como en nginx)
	bool allowed = _location->isMethodAllowed(method) ||
				   (method == "HEAD" && _location->isMethodAllowed("GET"));
	if (!allowed)
	{
		// HTTP obliga a decir en "Allow" qué métodos SÍ se aceptan
		HttpResponse response = errorResponse(_server, 405);
		std::string allow;
		const std::vector<std::string>& methods = _location->getAllowedMethods();
		for (size_t i = 0; i < methods.size(); ++i)
			allow += (i ? ", " : "") + methods[i];
		response.setHeader("Allow", allow);
		return response;
	}

	// 3) Redirección configurada con "return"
	if (_location->getReturnCode() != 0)
		return redirect(_location->getReturnCode(), _location->getReturn());

	std::string uri_path = _request.getPath();
	std::string path = mapPath(uri_path);

	// 4a) Si piden un directorio cuyo index es un script ("index time.py" en
	//     /cgi-bin), tratamos la petición como si fuera al script
	std::string index = indexName();
	if (method != "DELETE" && uri_path[uri_path.size() - 1] == '/' && isDirectory(path) &&
		!index.empty() && isRegularFile(joinPath(path, index)))
		uri_path = joinPath(uri_path, index);

	// 4b) ¿Es un CGI? Entonces lo lanzamos y la respuesta llegará más tarde
	std::string script_uri;
	std::string path_info;
	if (method != "DELETE" && findCgiScript(uri_path, script_uri, path_info))
	{
		if (startCgi(script_uri, path_info, cgi))
			return HttpResponse();  // se ignora: el Client esperará al CGI
		return errorResponse(_server, 500);
	}

	// 5) Contenido estático
	if (method == "GET" || method == "HEAD")
		return handleGet(path);
	if (method == "POST")
		return handlePost(path);
	return handleDelete(path);
}

/* ======================================================================
 *  GET / HEAD
 * ====================================================================== */

HttpResponse RequestHandler::handleGet(const std::string& path)
{
	if (isDirectory(path))
	{
		// "/tours" -> 301 a "/tours/". Sin la barra final, los enlaces
		// relativos de la página (p.ej. "tours2.html") se resolverían mal en
		// el navegador ("/tours2.html" en lugar de "/tours/tours2.html").
		const std::string& uri_path = _request.getPath();
		if (uri_path[uri_path.size() - 1] != '/')
		{
			std::string target = uri_path + "/";
			if (!_request.getQuery().empty())
				target += "?" + _request.getQuery();
			return redirect(301, target);
		}
		// Directorio: primero el index, si no el listado, si no prohibido
		std::string index = indexName();
		if (!index.empty() && isRegularFile(joinPath(path, index)))
			return serveFile(joinPath(path, index));
		if (_location->getAutoindex())
			return autoindex(path);
		return errorResponse(_server, 403);
	}
	if (!isRegularFile(path))
		return errorResponse(_server, 404);
	return serveFile(path);
}

HttpResponse RequestHandler::serveFile(const std::string& path)
{
	// access(R_OK): ¿tenemos permiso de lectura? Si no, 403 Forbidden
	std::string content;
	if (access(path.c_str(), R_OK) != 0 || !readFile(path, content))
		return errorResponse(_server, 403);
	HttpResponse response;
	response.setBody(content, mimeType(path));
	return response;
}

// "autoindex on": genera una página HTML con el contenido del directorio
HttpResponse RequestHandler::autoindex(const std::string& path)
{
	DIR* dir = opendir(path.c_str());
	if (dir == NULL)
		return errorResponse(_server, 403);
	std::vector<std::string> entries;
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL)
	{
		std::string name = entry->d_name;
		if (name == ".")
			continue;  // "." no aporta nada; ".." sí (para subir)
		if (isDirectory(joinPath(path, name)))
			name += "/";  // así el enlace apunta al directorio directamente
		entries.push_back(name);
	}
	closedir(dir);
	std::sort(entries.begin(), entries.end());  // readdir no garantiza orden

	std::string title = "Index of " + htmlEscape(_request.getPath());
	std::string body = "<!DOCTYPE html>\n<html>\n<head><title>" + title +
					   "</title></head>\n<body>\n<h1>" + title + "</h1>\n<hr>\n<ul>\n";
	for (size_t i = 0; i < entries.size(); ++i)
		body += "<li><a href=\"" + htmlEscape(entries[i]) + "\">" + htmlEscape(entries[i]) +
				"</a></li>\n";
	body += "</ul>\n<hr>\n</body>\n</html>\n";
	HttpResponse response;
	response.setBody(body, "text/html");
	return response;
}

/* ======================================================================
 *  POST: subida de ficheros
 * ====================================================================== */

HttpResponse RequestHandler::handlePost(const std::string& path)
{
	(void)path;
	// Sin CGI ni upload_store no hay nada que hacer con el body: como nginx
	// con un fichero estático, respondemos 405
	if (_location->getUploadStore().empty())
	{
		HttpResponse response = errorResponse(_server, 405);
		response.setHeader("Allow", "GET, HEAD");
		return response;
	}
	// La carpeta configurada no existe: error de configuración del servidor
	if (!isDirectory(_location->getUploadStore()))
		return errorResponse(_server, 500);
	return saveUploads(_location->getUploadStore());
}

/*
 * Un nombre de fichero subido no puede contener '/' ni ser ".." : si no, un
 * cliente podría escribir fuera de la carpeta de subidas ("../../etc/x").
 */
static bool safeFilename(const std::string& name)
{
	return !name.empty() && name != "." && name != ".." &&
		   name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}

// Escribe un fichero en disco (binario; sobrescribe si ya existe)
static bool writeFile(const std::string& path, const std::string& content)
{
	std::ofstream file(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!file)
		return false;
	file.write(content.data(), content.size());
	return file.good();
}

/*
 * Extrae un parámetro de una cabecera:
 *   headerParameter("multipart/form-data; boundary=XYZ", "boundary") -> "XYZ"
 *   headerParameter("...; filename=\"a b.txt\"", "filename")          -> "a b.txt"
 */
static std::string headerParameter(const std::string& header, const std::string& name)
{
	std::string lower = toLower(header);
	size_t pos = lower.find(name + "=");
	if (pos == std::string::npos)
		return "";
	pos += name.size() + 1;
	if (pos < header.size() && header[pos] == '"')
	{
		// Valor entre comillas
		size_t end = header.find('"', pos + 1);
		return header.substr(pos + 1, end == std::string::npos ? std::string::npos
															   : end - pos - 1);
	}
	// Valor sin comillas: hasta el siguiente ';' o el final
	size_t end = header.find(';', pos);
	return trim(header.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
}

/*
 * Dos formas de subir un fichero:
 *
 * A) multipart/form-data (lo que envía un <form enctype="multipart/form-data">).
 *    El body son "partes" separadas por una frontera (boundary):
 *
 *      --XYZ\r\n
 *      Content-Disposition: form-data; name="fichero"; filename="foto.png"\r\n
 *      Content-Type: image/png\r\n
 *      \r\n
 *      <bytes del fichero>\r\n
 *      --XYZ--\r\n                 <- "--" al final = última frontera
 *
 *    Guardamos cada parte que tenga filename.
 *
 * B) Body "crudo" (p.ej. curl --data-binary @f /uploads/f.txt): el body ES el
 *    fichero; el nombre sale de la URI o se genera uno.
 */
HttpResponse RequestHandler::saveUploads(const std::string& directory)
{
	std::vector<std::string> saved;
	std::string content_type = _request.getHeader("content-type");
	const std::string& body = _request.getBody();

	if (toLower(content_type).find("multipart/form-data") == 0)
	{
		std::string boundary = headerParameter(content_type, "boundary");
		if (boundary.empty())
			return errorResponse(_server, 400);
		std::string delimiter = "--" + boundary;
		// Saltamos hasta justo después de la primera frontera
		size_t pos = body.find(delimiter);
		if (pos == std::string::npos)
			return errorResponse(_server, 400);
		pos += delimiter.size();
		// Mientras no sea la frontera final ("--XYZ--")
		while (body.compare(pos, 2, "--") != 0)
		{
			// Cabeceras de la parte, hasta la línea vacía
			size_t header_end = body.find("\r\n\r\n", pos);
			if (header_end == std::string::npos)
				return errorResponse(_server, 400);
			std::string headers = body.substr(pos, header_end - pos);
			// Contenido de la parte: hasta la siguiente frontera
			size_t content_start = header_end + 4;
			size_t next = body.find("\r\n" + delimiter, content_start);
			if (next == std::string::npos)
				return errorResponse(_server, 400);
			std::string filename = headerParameter(headers, "filename");
			// Algunos navegadores mandan la ruta completa ("C:\fotos\a.png"):
			// nos quedamos solo con el nombre
			size_t slash = filename.find_last_of("/\\");
			if (slash != std::string::npos)
				filename = filename.substr(slash + 1);
			// Las partes sin filename son campos de texto normales: se ignoran
			if (!filename.empty())
			{
				if (!safeFilename(filename))
					return errorResponse(_server, 400);
				if (!writeFile(joinPath(directory, filename),
							   body.substr(content_start, next - content_start)))
					return errorResponse(_server, 500);
				saved.push_back(filename);
			}
			// Avanzamos a la siguiente frontera ("\r\n" + delimiter)
			pos = next + 2 + delimiter.size();
		}
		if (saved.empty())
			return errorResponse(_server, 400);  // formulario sin ficheros
	}
	else
	{
		// "/uploads/a.txt" -> "a.txt". Si la URI es la propia location
		// ("/uploads") inventamos un nombre único con la hora y un contador
		std::string filename;
		const std::string& uri_path = _request.getPath();
		if (uri_path.size() > _location->getPath().size() && uri_path[uri_path.size() - 1] != '/')
			filename = uri_path.substr(uri_path.rfind('/') + 1);
		if (filename.empty())
		{
			static unsigned long counter = 0;
			filename = "upload_" + toString(std::time(NULL)) + "_" + toString(counter++);
		}
		if (!safeFilename(filename))
			return errorResponse(_server, 400);
		if (!writeFile(joinPath(directory, filename), body))
			return errorResponse(_server, 500);
		saved.push_back(filename);
	}

	// 201 Created + una página con lo que se ha guardado
	std::string html = "<!DOCTYPE html>\n<html><body>\n<h1>Upload complete</h1>\n<ul>\n";
	for (size_t i = 0; i < saved.size(); ++i)
		html += "<li>" + htmlEscape(saved[i]) + "</li>\n";
	html += "</ul>\n</body></html>\n";
	HttpResponse response;
	response.setStatus(201);
	response.setBody(html, "text/html");
	return response;
}

/* ======================================================================
 *  DELETE
 * ====================================================================== */

HttpResponse RequestHandler::handleDelete(const std::string& path)
{
	// Borrar directorios no está soportado: 409 Conflict
	if (isDirectory(path))
		return errorResponse(_server, 409);
	if (!isRegularFile(path))
		return errorResponse(_server, 404);
	// Para borrar un fichero hace falta permiso de escritura en su carpeta
	std::string parent = path.substr(0, path.rfind('/') + 1);
	if (parent.empty())
		parent = ".";
	// std::remove es de la librería estándar de C++98 (unlink no está permitida)
	if (access(parent.c_str(), W_OK) != 0 || std::remove(path.c_str()) != 0)
		return errorResponse(_server, 403);
	// 204 No Content: hecho, y no hay nada que devolver
	HttpResponse response;
	response.setStatus(204);
	return response;
}

/* ======================================================================
 *  CGI
 * ====================================================================== */

/*
 * La comunicación servidor -> script se hace con variables de entorno
 * (estándar CGI/1.1, RFC 3875) más el body por stdin.
 */
bool RequestHandler::startCgi(const std::string& script_uri, const std::string& path_info,
							  Cgi*& cgi)
{
	std::string interpreter;
	_location->getCgiInterpreter(getExtension(script_uri), interpreter);
	std::string script_path = mapPath(script_uri);

	std::vector<std::string> env;
	std::string server_name = _server.getServerNames().empty() ? _server.getHost()
															   : _server.getServerNames()[0];
	// Información del servidor
	env.push_back("GATEWAY_INTERFACE=CGI/1.1");
	env.push_back("SERVER_PROTOCOL=HTTP/1.1");
	env.push_back("SERVER_SOFTWARE=webserv");
	env.push_back("SERVER_NAME=" + server_name);
	env.push_back("SERVER_PORT=" + toString(_port));
	// Información de la petición
	env.push_back("REMOTE_ADDR=" + _client_address);
	env.push_back("REQUEST_METHOD=" + _request.getMethod());
	env.push_back("REQUEST_URI=" + _request.getTarget());
	env.push_back("QUERY_STRING=" + _request.getQuery());       // lo que va tras '?'
	env.push_back("SCRIPT_NAME=" + script_uri);                 // URI del script
	// El script se ejecuta dentro de su carpeta, así que basta su nombre
	env.push_back("SCRIPT_FILENAME=" + script_path.substr(script_path.rfind('/') + 1));
	env.push_back("PATH_INFO=" + path_info);                    // lo que va tras el script
	if (!path_info.empty())
		env.push_back("PATH_TRANSLATED=" + mapPath(path_info));
	env.push_back("REDIRECT_STATUS=200");  // php-cgi se niega a funcionar sin ella
	// Body: el script debe leer exactamente CONTENT_LENGTH bytes de stdin
	// (con chunked, es el tamaño ya des-chunkeado)
	env.push_back("CONTENT_LENGTH=" + toString(_request.getBody().size()));
	if (!_request.getHeader("content-type").empty())
		env.push_back("CONTENT_TYPE=" + _request.getHeader("content-type"));

	// Cada cabecera HTTP pasa como HTTP_<NOMBRE>: "user-agent" -> HTTP_USER_AGENT
	const std::map<std::string, std::string>& headers = _request.getHeaders();
	for (std::map<std::string, std::string>::const_iterator it = headers.begin();
		 it != headers.end(); ++it)
	{
		if (it->first == "content-type" || it->first == "content-length")
			continue;  // ya van como CONTENT_TYPE / CONTENT_LENGTH
		std::string name = "HTTP_";
		for (size_t i = 0; i < it->first.size(); ++i)
			name += it->first[i] == '-' ? '_' : std::toupper(it->first[i]);
		env.push_back(name + "=" + it->second);
	}

	// El Client se queda con este objeto y lo borrará al terminar
	cgi = new Cgi();
	if (!cgi->start(interpreter, script_path, env, _request.getBody()))
	{
		delete cgi;
		cgi = NULL;
		return false;
	}
	return true;
}
