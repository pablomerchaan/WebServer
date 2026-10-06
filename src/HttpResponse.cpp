#include "../inc/HttpResponse.hpp"
#include "../inc/Utils.hpp"

HttpResponse::HttpResponse() : _status(200) {}

void HttpResponse::setStatus(int status) { _status = status; }

void HttpResponse::setHeader(const std::string& name, const std::string& value)
{
	_headers[name] = value;
}

void HttpResponse::setBody(const std::string& body, const std::string& content_type)
{
	_body = body;
	_headers["Content-Type"] = content_type;
}

void HttpResponse::setBody(const std::string& body) { _body = body; }

int HttpResponse::getStatus() const { return _status; }

bool HttpResponse::hasHeader(const std::string& name) const
{
	return _headers.count(name) != 0;
}

std::string HttpResponse::build(bool keep_alive, bool include_body) const
{
	// 1) Línea de estado: versión, código y frase
	std::string result = "HTTP/1.1 " + toString(_status) + " " + statusReason(_status) + "\r\n";

	// 2) Cabeceras fijas que lleva toda respuesta
	result += "Server: webserv\r\n";
	result += "Date: " + httpDate() + "\r\n";

	// 3) Cabeceras propias de esta respuesta (Content-Type, Location, Allow...)
	for (std::map<std::string, std::string>::const_iterator it = _headers.begin();
		 it != _headers.end(); ++it)
		result += it->first + ": " + it->second + "\r\n";

	// 4) Content-Length SIEMPRE: así el cliente sabe exactamente dónde acaba
	//    esta respuesta y puede reutilizar la conexión para la siguiente
	//    (keep-alive). En HEAD se envía el tamaño que TENDRÍA el cuerpo.
	result += "Content-Length: " + toString(_body.size()) + "\r\n";

	// 5) Connection: avisa de si vamos a cerrar la conexión tras responder
	result += std::string("Connection: ") + (keep_alive ? "keep-alive" : "close") + "\r\n";

	// 6) Línea vacía que separa cabeceras y cuerpo, y el cuerpo
	result += "\r\n";
	if (include_body)
		result += _body;
	return result;
}
