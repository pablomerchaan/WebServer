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

	_fallback.setPath("/");
	_location = _server.matchLocation(_request.getPath());
	if (_location == NULL)
		_location = &_fallback;
}

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

unsigned long RequestHandler::maxBodySize(const Server& server, const HttpRequest& request)
{
	const Location* location = server.matchLocation(request.getPath());
	if (location != NULL && location->hasClientMaxBodySize())
		return location->getClientMaxBodySize();
	return server.getClientMaxBodySize();
}

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

bool RequestHandler::findCgiScript(const std::string& uri_path, std::string& script_uri,
								   std::string& path_info) const
{
	if (_location->getCgiExtensions().empty())
		return false;
	for (size_t end = 1; end <= uri_path.size(); ++end)
	{
		if (end != uri_path.size() && uri_path[end] != '/')
			continue;
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

HttpResponse RequestHandler::redirect(int code, const std::string& location)
{
	HttpResponse response;
	response.setStatus(code);
	response.setHeader("Location", location);

	response.setBody("<html><body><a href=\"" + htmlEscape(location) + "\">" +
						 toString(code) + " " + statusReason(code) + "</a></body></html>\n",
					 "text/html");
	return response;
}

HttpResponse RequestHandler::handle(Cgi*& cgi)
{
	cgi = NULL;
	const std::string& method = _request.getMethod();

	if (method != "GET" && method != "HEAD" && method != "POST" && method != "DELETE")
		return errorResponse(_server, 501);

	bool allowed = _location->isMethodAllowed(method) ||
				   (method == "HEAD" && _location->isMethodAllowed("GET"));
	if (!allowed)
	{

		HttpResponse response = errorResponse(_server, 405);
		std::string allow;
		const std::vector<std::string>& methods = _location->getAllowedMethods();
		for (size_t i = 0; i < methods.size(); ++i)
			allow += (i ? ", " : "") + methods[i];
		response.setHeader("Allow", allow);
		return response;
	}

	if (_location->getReturnCode() != 0)
		return redirect(_location->getReturnCode(), _location->getReturn());

	std::string uri_path = _request.getPath();
	std::string path = mapPath(uri_path);

	std::string index = indexName();
	if (method != "DELETE" && uri_path[uri_path.size() - 1] == '/' && isDirectory(path) &&
		!index.empty() && isRegularFile(joinPath(path, index)))
		uri_path = joinPath(uri_path, index);

	std::string script_uri;
	std::string path_info;
	if (method != "DELETE" && findCgiScript(uri_path, script_uri, path_info))
	{
		if (startCgi(script_uri, path_info, cgi))
			return HttpResponse();
		return errorResponse(_server, 500);
	}

	if (method == "GET" || method == "HEAD")
		return handleGet(path);
	if (method == "POST")
		return handlePost(path);
	return handleDelete(path);
}

HttpResponse RequestHandler::handleGet(const std::string& path)
{
	if (isDirectory(path))
	{

		const std::string& uri_path = _request.getPath();
		if (uri_path[uri_path.size() - 1] != '/')
		{
			std::string target = uri_path + "/";
			if (!_request.getQuery().empty())
				target += "?" + _request.getQuery();
			return redirect(301, target);
		}

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

	std::string content;
	if (access(path.c_str(), R_OK) != 0 || !readFile(path, content))
		return errorResponse(_server, 403);
	HttpResponse response;
	response.setBody(content, mimeType(path));
	return response;
}

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
			continue;
		if (isDirectory(joinPath(path, name)))
			name += "/";
		entries.push_back(name);
	}
	closedir(dir);
	std::sort(entries.begin(), entries.end());

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

HttpResponse RequestHandler::handlePost(const std::string& path)
{
	(void)path;

	if (_location->getUploadStore().empty())
	{
		HttpResponse response = errorResponse(_server, 405);
		response.setHeader("Allow", "GET, HEAD");
		return response;
	}

	if (!isDirectory(_location->getUploadStore()))
		return errorResponse(_server, 500);
	return saveUploads(_location->getUploadStore());
}

static bool safeFilename(const std::string& name)
{
	return !name.empty() && name != "." && name != ".." &&
		   name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}

static bool writeFile(const std::string& path, const std::string& content)
{
	std::ofstream file(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!file)
		return false;
	file.write(content.data(), content.size());
	return file.good();
}

static std::string headerParameter(const std::string& header, const std::string& name)
{
	std::string lower = toLower(header);
	size_t pos = lower.find(name + "=");
	if (pos == std::string::npos)
		return "";
	pos += name.size() + 1;
	if (pos < header.size() && header[pos] == '"')
	{

		size_t end = header.find('"', pos + 1);
		return header.substr(pos + 1, end == std::string::npos ? std::string::npos
															   : end - pos - 1);
	}

	size_t end = header.find(';', pos);
	return trim(header.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
}

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

		size_t pos = body.find(delimiter);
		if (pos == std::string::npos)
			return errorResponse(_server, 400);
		pos += delimiter.size();

		while (body.compare(pos, 2, "--") != 0)
		{

			size_t header_end = body.find("\r\n\r\n", pos);
			if (header_end == std::string::npos)
				return errorResponse(_server, 400);
			std::string headers = body.substr(pos, header_end - pos);

			size_t content_start = header_end + 4;
			size_t next = body.find("\r\n" + delimiter, content_start);
			if (next == std::string::npos)
				return errorResponse(_server, 400);
			std::string filename = headerParameter(headers, "filename");

			size_t slash = filename.find_last_of("/\\");
			if (slash != std::string::npos)
				filename = filename.substr(slash + 1);

			if (!filename.empty())
			{
				if (!safeFilename(filename))
					return errorResponse(_server, 400);
				if (!writeFile(joinPath(directory, filename),
							   body.substr(content_start, next - content_start)))
					return errorResponse(_server, 500);
				saved.push_back(filename);
			}

			pos = next + 2 + delimiter.size();
		}
		if (saved.empty())
			return errorResponse(_server, 400);
	}
	else
	{

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

	std::string html = "<!DOCTYPE html>\n<html><body>\n<h1>Upload complete</h1>\n<ul>\n";
	for (size_t i = 0; i < saved.size(); ++i)
		html += "<li>" + htmlEscape(saved[i]) + "</li>\n";
	html += "</ul>\n</body></html>\n";
	HttpResponse response;
	response.setStatus(201);
	response.setBody(html, "text/html");
	return response;
}

HttpResponse RequestHandler::handleDelete(const std::string& path)
{

	if (isDirectory(path))
		return errorResponse(_server, 409);
	if (!isRegularFile(path))
		return errorResponse(_server, 404);

	std::string parent = path.substr(0, path.rfind('/') + 1);
	if (parent.empty())
		parent = ".";

	if (access(parent.c_str(), W_OK) != 0 || std::remove(path.c_str()) != 0)
		return errorResponse(_server, 403);

	HttpResponse response;
	response.setStatus(204);
	return response;
}

bool RequestHandler::startCgi(const std::string& script_uri, const std::string& path_info,
							  Cgi*& cgi)
{
	std::string interpreter;
	_location->getCgiInterpreter(getExtension(script_uri), interpreter);
	std::string script_path = mapPath(script_uri);

	std::vector<std::string> env;
	std::string server_name = _server.getServerNames().empty() ? _server.getHost()
															   : _server.getServerNames()[0];

	env.push_back("GATEWAY_INTERFACE=CGI/1.1");
	env.push_back("SERVER_PROTOCOL=HTTP/1.1");
	env.push_back("SERVER_SOFTWARE=webserv");
	env.push_back("SERVER_NAME=" + server_name);
	env.push_back("SERVER_PORT=" + toString(_port));

	env.push_back("REMOTE_ADDR=" + _client_address);
	env.push_back("REQUEST_METHOD=" + _request.getMethod());
	env.push_back("REQUEST_URI=" + _request.getTarget());
	env.push_back("QUERY_STRING=" + _request.getQuery());
	env.push_back("SCRIPT_NAME=" + script_uri);

	env.push_back("SCRIPT_FILENAME=" + script_path.substr(script_path.rfind('/') + 1));
	env.push_back("PATH_INFO=" + path_info);
	if (!path_info.empty())
		env.push_back("PATH_TRANSLATED=" + mapPath(path_info));
	env.push_back("REDIRECT_STATUS=200");

	env.push_back("CONTENT_LENGTH=" + toString(_request.getBody().size()));
	if (!_request.getHeader("content-type").empty())
		env.push_back("CONTENT_TYPE=" + _request.getHeader("content-type"));

	const std::map<std::string, std::string>& headers = _request.getHeaders();
	for (std::map<std::string, std::string>::const_iterator it = headers.begin();
		 it != headers.end(); ++it)
	{
		if (it->first == "content-type" || it->first == "content-length")
			continue;
		std::string name = "HTTP_";
		for (size_t i = 0; i < it->first.size(); ++i)
			name += it->first[i] == '-' ? '_' : std::toupper(it->first[i]);
		env.push_back(name + "=" + it->second);
	}

	cgi = new Cgi();
	if (!cgi->start(interpreter, script_path, env, _request.getBody()))
	{
		delete cgi;
		cgi = NULL;
		return false;
	}
	return true;
}
