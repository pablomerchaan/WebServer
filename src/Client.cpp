#include "../inc/Client.hpp"
#include "../inc/RequestHandler.hpp"
#include <sys/socket.h>
#include <unistd.h>

static const std::time_t CLIENT_TIMEOUT = 60;
static const std::time_t CGI_TIMEOUT = 10;
static const size_t SEND_CHUNK = 65536;

Client::Client(int fd, const std::string& address, int port,
			   const std::vector<const Server*>& servers)
	: _fd(fd), _address(address), _port(port), _servers(servers), _server(servers[0]),
	  _state(READING), _write_offset(0), _keep_alive(true), _cgi(NULL),
	  _last_activity(std::time(NULL))
{
}

Client::~Client()
{
	delete _cgi;
	close(_fd);
}

int Client::getFd() const { return _fd; }
Client::State Client::getState() const { return _state; }
Cgi* Client::getCgi() const { return _cgi; }

bool Client::onReadable()
{
	char buffer[65536];
	ssize_t bytes = recv(_fd, buffer, sizeof(buffer), 0);

	if (bytes <= 0)
		return false;
	_last_activity = std::time(NULL);
	_request.append(buffer, bytes);
	processInput();
	return true;
}

bool Client::onWritable()
{

	size_t remaining = _write_buffer.size() - _write_offset;
	size_t length = remaining < SEND_CHUNK ? remaining : SEND_CHUNK;
	ssize_t sent = send(_fd, _write_buffer.data() + _write_offset, length, 0);
	if (sent <= 0)
		return false;
	_last_activity = std::time(NULL);
	_write_offset += sent;
	if (_write_offset < _write_buffer.size())
		return true;

	if (!_keep_alive)
		return false;

	_write_buffer.clear();
	_write_offset = 0;
	_request.reset();
	_server = _servers[0];
	_state = READING;

	if (_request.hasBufferedData())
		processInput();
	return true;
}

void Client::processInput()
{
	HttpRequest::State state = _request.parse();
	if (state == HttpRequest::HEADERS_DONE)
	{

		_server = &RequestHandler::selectServer(_servers, _request);
		_request.setMaxBodySize(RequestHandler::maxBodySize(*_server, _request));
		state = _request.getState();
	}
	if (state == HttpRequest::ERROR)
		queueError(_request.getErrorCode());
	else if (state == HttpRequest::COMPLETE)
		dispatch();

}

void Client::dispatch()
{
	RequestHandler handler(*_server, _request, _address, _port);
	Cgi* cgi = NULL;
	HttpResponse response = handler.handle(cgi);
	if (cgi != NULL)
	{

		_cgi = cgi;
		_state = CGI;
		return;
	}
	queueResponse(response, _request.keepAlive());
}

void Client::queueResponse(const HttpResponse& response, bool keep_alive)
{
	_keep_alive = keep_alive;
	_write_buffer = response.build(keep_alive, _request.getMethod() != "HEAD");
	_write_offset = 0;
	_state = WRITING;
}

void Client::queueError(int code)
{

	queueResponse(RequestHandler::errorResponse(*_server, code), false);
}

void Client::onCgiWritable()
{
	if (_cgi != NULL)
		_cgi->writeInput();
}

void Client::onCgiReadable()
{

	if (_cgi != NULL && !_cgi->readOutput())
		finishCgi(0);
}

void Client::finishCgi(int error_code)
{
	HttpResponse response;
	if (error_code == 0)
		response = _cgi->buildResponse();
	delete _cgi;
	_cgi = NULL;
	_last_activity = std::time(NULL);

	if (error_code != 0 || response.getStatus() == 502)
		queueResponse(RequestHandler::errorResponse(*_server, error_code ? error_code : 502),
					  _request.keepAlive());
	else
		queueResponse(response, _request.keepAlive());
}

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

	if (_state == READING &&
		(_request.hasBufferedData() || _request.getState() != HttpRequest::HEADERS))
	{
		_last_activity = now;
		queueError(408);
		return true;
	}
	return false;
}
