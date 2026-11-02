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

	std::string result = "HTTP/1.1 " + toString(_status) + " " + statusReason(_status) + "\r\n";

	result += "Server: webserv\r\n";
	result += "Date: " + httpDate() + "\r\n";

	for (std::map<std::string, std::string>::const_iterator it = _headers.begin();
		 it != _headers.end(); ++it)
		result += it->first + ": " + it->second + "\r\n";

	result += "Content-Length: " + toString(_body.size()) + "\r\n";

	result += std::string("Connection: ") + (keep_alive ? "keep-alive" : "close") + "\r\n";

	result += "\r\n";
	if (include_body)
		result += _body;
	return result;
}
