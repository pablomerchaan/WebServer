#include "../inc/HttpRequest.hpp"
#include "../inc/Utils.hpp"
#include <cstdlib>

static const size_t MAX_HEADER_SIZE = 16384;
static const size_t MAX_URI_SIZE = 8192;

HttpRequest::HttpRequest()
	: _state(HEADERS), _content_length(0), _chunk_remaining(0), _max_body_size(0),
	  _error_code(0)
{
}

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

HttpRequest::State HttpRequest::parse()
{
	if (_state == HEADERS)
	{

		size_t end = _buffer.find("\r\n\r\n");
		size_t separator = 4;
		if (end == std::string::npos)
		{

			end = _buffer.find("\n\n");
			separator = 2;
		}
		if (end == std::string::npos)
		{

			if (_buffer.size() > MAX_HEADER_SIZE)
				return fail(_buffer.find('\n') == std::string::npos ? 414 : 431);
			return _state;
		}
		if (end > MAX_HEADER_SIZE)
			return fail(431);

		std::string section = _buffer.substr(0, end);
		_buffer.erase(0, end + separator);
		if (!parseHeaderSection(section))
			return _state;

		_state = HEADERS_DONE;
	}
	if (_state == BODY)
		parseBody();
	if (_state == CHUNK_SIZE || _state == CHUNK_DATA || _state == CHUNK_TRAILER)
		parseChunks();
	return _state;
}

bool HttpRequest::parseHeaderSection(const std::string& section)
{

	size_t start = section.find_first_not_of("\r\n");
	if (start == std::string::npos)
		return reject(400);

	size_t line_end = section.find('\n', start);
	std::string request_line = section.substr(start, line_end == std::string::npos
														 ? std::string::npos : line_end - start);
	if (!request_line.empty() && request_line[request_line.size() - 1] == '\r')
		request_line.erase(request_line.size() - 1);
	if (!parseRequestLine(request_line))
		return false;

	while (line_end != std::string::npos)
	{
		size_t next = section.find('\n', line_end + 1);
		std::string line = section.substr(line_end + 1, next == std::string::npos
															? std::string::npos : next - line_end - 1);
		line_end = next;
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		if (line.empty())
			continue;
		size_t colon = line.find(':');
		if (colon == std::string::npos || colon == 0)
			return reject(400);

		std::string name = toLower(line.substr(0, colon));
		if (name.find_first_of(" \t") != std::string::npos)
			return reject(400);
		std::string value = trim(line.substr(colon + 1));
		if (_headers.count(name))
		{

			if (name == "host" || name == "content-length")
			{
				if (_headers[name] != value)
					return reject(400);
			}
			else
				_headers[name] += ", " + value;
		}
		else
			_headers[name] = value;
	}

	if (_version == "HTTP/1.1" && !_headers.count("host"))
		return reject(400);

	if (_headers.count("transfer-encoding"))
	{

		if (toLower(_headers["transfer-encoding"]) != "chunked")
			return reject(501);

		if (_headers.count("content-length"))
			return reject(400);
	}
	else if (_headers.count("content-length"))
	{

		const std::string& value = _headers["content-length"];
		if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos ||
			value.size() > 18)
			return reject(400);
		_content_length = std::strtoul(value.c_str(), NULL, 10);
	}
	return true;
}

bool HttpRequest::parseRequestLine(const std::string& line)
{

	size_t first = line.find(' ');
	size_t last = line.rfind(' ');
	if (first == std::string::npos || first == last)
		return reject(400);
	_method = line.substr(0, first);
	_target = line.substr(first + 1, last - first - 1);
	_version = line.substr(last + 1);

	if (_method.empty() || _method.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos)
		return reject(400);
	if (_target.empty() || _target.find(' ') != std::string::npos)
		return reject(400);
	if (_target.size() > MAX_URI_SIZE)
		return reject(414);
	if (_version.compare(0, 5, "HTTP/") != 0)
		return reject(400);

	if (_version != "HTTP/1.1" && _version != "HTTP/1.0")
		return reject(505);
	return parseTarget();
}

bool HttpRequest::parseTarget()
{
	std::string target = _target;

	if (target.compare(0, 7, "http://") == 0)
	{
		size_t slash = target.find('/', 7);
		target = slash == std::string::npos ? "/" : target.substr(slash);
	}
	if (target[0] != '/')
		return reject(400);

	size_t hash = target.find('#');
	if (hash != std::string::npos)
		target.erase(hash);

	size_t question = target.find('?');
	if (question != std::string::npos)
	{
		_query = target.substr(question + 1);
		target.erase(question);
	}

	std::string decoded;
	if (!urlDecode(target, decoded) || !normalizePath(decoded, _path))
		return reject(400);
	return true;
}

void HttpRequest::setMaxBodySize(unsigned long size)
{
	if (_state != HEADERS_DONE)
		return;
	_max_body_size = size;
	if (_headers.count("transfer-encoding"))
		_state = CHUNK_SIZE;
	else if (_content_length > 0)
	{

		if (_max_body_size != 0 && _content_length > _max_body_size)
		{
			fail(413);
			return;
		}
		_state = BODY;
	}
	else
		_state = COMPLETE;

	parse();
}

void HttpRequest::parseBody()
{
	size_t needed = _content_length - _body.size();
	size_t take = needed < _buffer.size() ? needed : _buffer.size();
	_body.append(_buffer, 0, take);
	_buffer.erase(0, take);
	if (_body.size() == _content_length)
		_state = COMPLETE;
}

void HttpRequest::parseChunks()
{
	while (true)
	{
		if (_state == CHUNK_SIZE)
		{
			size_t end = _buffer.find("\r\n");
			if (end == std::string::npos)
			{

				if (_buffer.size() > 1024)
					fail(400);
				return;
			}
			std::string line = _buffer.substr(0, end);
			_buffer.erase(0, end + 2);

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
				_state = CHUNK_TRAILER;
			else if (_max_body_size != 0 && _body.size() + _chunk_remaining > _max_body_size)
			{
				fail(413);
				return;
			}
			else
				_state = CHUNK_DATA;
		}
		else if (_state == CHUNK_DATA)
		{

			if (_buffer.size() < _chunk_remaining + 2)
				return;
			if (_buffer.compare(_chunk_remaining, 2, "\r\n") != 0)
			{
				fail(400);
				return;
			}
			_body.append(_buffer, 0, _chunk_remaining);
			_buffer.erase(0, _chunk_remaining + 2);
			_state = CHUNK_SIZE;
		}
		else if (_state == CHUNK_TRAILER)
		{

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

bool HttpRequest::keepAlive() const
{
	std::string connection = toLower(getHeader("connection"));
	if (_version == "HTTP/1.0")
		return connection == "keep-alive";
	return connection != "close";
}
