#include "../inc/Utils.hpp"
#include <cctype>
#include <ctime>
#include <fstream>
#include <vector>
#include <sys/stat.h>

std::string toLower(const std::string& str)
{
	std::string result(str);

	for (size_t i = 0; i < result.size(); ++i)
		result[i] = std::tolower(static_cast<unsigned char>(result[i]));
	return result;
}

std::string trim(const std::string& str)
{
	size_t start = str.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return "";
	size_t end = str.find_last_not_of(" \t\r\n");
	return str.substr(start, end - start + 1);
}

bool endsWith(const std::string& str, const std::string& suffix)
{
	return str.size() >= suffix.size() &&
		   str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string joinPath(const std::string& base, const std::string& path)
{
	if (base.empty())
		return path;
	if (path.empty())
		return base;
	bool base_slash = base[base.size() - 1] == '/';
	bool path_slash = path[0] == '/';
	if (base_slash && path_slash)
		return base + path.substr(1);
	if (!base_slash && !path_slash)
		return base + "/" + path;
	return base + path;
}

std::string getExtension(const std::string& path)
{
	size_t slash = path.rfind('/');
	size_t dot = path.rfind('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return "";
	return path.substr(dot);
}

std::string statusReason(int code)
{
	switch (code)
	{
		case 200: return "OK";
		case 201: return "Created";
		case 204: return "No Content";
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 303: return "See Other";
		case 307: return "Temporary Redirect";
		case 308: return "Permanent Redirect";
		case 400: return "Bad Request";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 408: return "Request Timeout";
		case 409: return "Conflict";
		case 411: return "Length Required";
		case 413: return "Content Too Large";
		case 414: return "URI Too Long";
		case 415: return "Unsupported Media Type";
		case 431: return "Request Header Fields Too Large";
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 502: return "Bad Gateway";
		case 503: return "Service Unavailable";
		case 504: return "Gateway Timeout";
		case 505: return "HTTP Version Not Supported";
		default: return "Unknown";
	}
}

std::string mimeType(const std::string& path)
{
	std::string ext = toLower(getExtension(path));
	if (ext == ".html" || ext == ".htm") return "text/html";
	if (ext == ".css") return "text/css";
	if (ext == ".js") return "application/javascript";
	if (ext == ".json") return "application/json";
	if (ext == ".txt") return "text/plain";
	if (ext == ".xml") return "application/xml";
	if (ext == ".png") return "image/png";
	if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
	if (ext == ".gif") return "image/gif";
	if (ext == ".svg") return "image/svg+xml";
	if (ext == ".ico") return "image/x-icon";
	if (ext == ".webp") return "image/webp";
	if (ext == ".pdf") return "application/pdf";
	if (ext == ".zip") return "application/zip";
	if (ext == ".mp4") return "video/mp4";
	if (ext == ".mp3") return "audio/mpeg";
	return "application/octet-stream";
}

std::string httpDate()
{
	char buffer[64];
	std::time_t now = std::time(NULL);
	std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&now));
	return buffer;
}

static int hexValue(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool urlDecode(const std::string& in, std::string& out)
{
	out.clear();
	for (size_t i = 0; i < in.size(); ++i)
	{
		if (in[i] != '%')
		{
			out += in[i];
			continue;
		}
		if (i + 2 >= in.size())
			return false;
		int high = hexValue(in[i + 1]);
		int low = hexValue(in[i + 2]);
		if (high < 0 || low < 0)
			return false;
		char decoded = static_cast<char>(high * 16 + low);
		if (decoded == '\0')
			return false;
		out += decoded;
		i += 2;
	}
	return true;
}

bool normalizePath(const std::string& in, std::string& out)
{
	std::vector<std::string> segments;
	size_t start = 0;
	while (start <= in.size())
	{
		size_t slash = in.find('/', start);
		if (slash == std::string::npos)
			slash = in.size();
		std::string segment = in.substr(start, slash - start);
		if (segment == "..")
		{
			if (segments.empty())
				return false;
			segments.pop_back();
		}
		else if (!segment.empty() && segment != ".")
			segments.push_back(segment);
		start = slash + 1;
	}

	out = "/";
	for (size_t i = 0; i < segments.size(); ++i)
	{
		out += segments[i];
		if (i + 1 < segments.size())
			out += "/";
	}

	if (segments.size() > 0 && in.size() > 1 && in[in.size() - 1] == '/')
		out += "/";
	return true;
}

std::string htmlEscape(const std::string& str)
{
	std::string result;
	for (size_t i = 0; i < str.size(); ++i)
	{
		if (str[i] == '<') result += "&lt;";
		else if (str[i] == '>') result += "&gt;";
		else if (str[i] == '&') result += "&amp;";
		else if (str[i] == '"') result += "&quot;";
		else result += str[i];
	}
	return result;
}

bool isDirectory(const std::string& path)
{
	struct stat info;
	return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool isRegularFile(const std::string& path)
{
	struct stat info;
	return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool readFile(const std::string& path, std::string& content)
{
	std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
	if (!file)
		return false;
	std::ostringstream buffer;
	buffer << file.rdbuf();
	content = buffer.str();
	return true;
}

std::string defaultErrorPage(int code)
{
	std::string status = toString(code) + " " + statusReason(code);
	return "<!DOCTYPE html>\n<html>\n<head><title>" + status + "</title></head>\n"
		   "<body>\n<center><h1>" + status + "</h1></center>\n"
		   "<hr><center>webserv</center>\n</body>\n</html>\n";
}
