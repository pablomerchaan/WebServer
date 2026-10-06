#include "../inc/Utils.hpp"
#include <cctype>
#include <ctime>
#include <fstream>
#include <vector>
#include <sys/stat.h>

/* ======================================================================
 *  Strings
 * ====================================================================== */

std::string toLower(const std::string& str)
{
	std::string result(str);
	// El cast a unsigned char evita comportamiento indefinido con chars negativos
	for (size_t i = 0; i < result.size(); ++i)
		result[i] = std::tolower(static_cast<unsigned char>(result[i]));
	return result;
}

std::string trim(const std::string& str)
{
	size_t start = str.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return "";  // solo había espacios
	size_t end = str.find_last_not_of(" \t\r\n");
	return str.substr(start, end - start + 1);
}

bool endsWith(const std::string& str, const std::string& suffix)
{
	return str.size() >= suffix.size() &&
		   str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/*
 * Une dos trozos de ruta con exactamente una '/' entre ellos:
 *   joinPath("www",  "a.html")  -> "www/a.html"
 *   joinPath("www/", "/a.html") -> "www/a.html"
 */
std::string joinPath(const std::string& base, const std::string& path)
{
	if (base.empty())
		return path;
	if (path.empty())
		return base;
	bool base_slash = base[base.size() - 1] == '/';
	bool path_slash = path[0] == '/';
	if (base_slash && path_slash)
		return base + path.substr(1);  // sobra una barra
	if (!base_slash && !path_slash)
		return base + "/" + path;      // falta una barra
	return base + path;
}

/*
 * Extensión del último segmento de la ruta, incluyendo el punto.
 * "/dir.v2/file" no tiene extensión: el punto está en un directorio.
 */
std::string getExtension(const std::string& path)
{
	size_t slash = path.rfind('/');
	size_t dot = path.rfind('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return "";
	return path.substr(dot);
}

/* ======================================================================
 *  HTTP
 * ====================================================================== */

// Frase estándar asociada a cada código (va en la primera línea de la respuesta)
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

/*
 * Tipo MIME según la extensión: va en la cabecera Content-Type y le dice al
 * navegador cómo interpretar el contenido (mostrar HTML, aplicar CSS, pintar
 * una imagen...). Si no lo conocemos, "application/octet-stream" = binario
 * genérico (el navegador lo descargará).
 */
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

// Ej: "Tue, 06 Oct 2026 11:08:00 GMT" (formato obligatorio de la cabecera Date)
std::string httpDate()
{
	char buffer[64];
	std::time_t now = std::time(NULL);
	std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&now));
	return buffer;
}

// Valor de un dígito hexadecimal, o -1 si no lo es
static int hexValue(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/*
 * En una URL los caracteres especiales van codificados como %XX (hex):
 * "mi%20fichero.txt" -> "mi fichero.txt". Rechazamos "%" incompletos o no
 * hexadecimales, y "%00" (un byte nulo cortaría la ruta al pasarla a C).
 */
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

/*
 * Normaliza una ruta trabajando segmento a segmento (como una pila):
 *   "."  -> se ignora
 *   ".." -> quita el segmento anterior; si no hay ninguno, la ruta intenta
 *           salir de la raíz -> devolvemos false (la petición será un 400)
 *   ""   -> (de "//") se ignora
 * "/a/./b/../c/" -> "/a/c/"
 */
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
	// Reconstruimos la ruta limpia
	out = "/";
	for (size_t i = 0; i < segments.size(); ++i)
	{
		out += segments[i];
		if (i + 1 < segments.size())
			out += "/";
	}
	// Conservamos la '/' final: sirve para distinguir "/tours" de "/tours/"
	// (al primero le mandaremos una redirección al segundo)
	if (segments.size() > 0 && in.size() > 1 && in[in.size() - 1] == '/')
		out += "/";
	return true;
}

// Evita que un nombre de fichero como "<script>" se interprete como HTML
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

/* ======================================================================
 *  Sistema de ficheros
 * ====================================================================== */

// stat() rellena información del fichero; S_ISDIR/S_ISREG miran su tipo
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

/*
 * Lee un fichero entero en modo binario (para imágenes, PDFs, etc.).
 * El subject permite leer ficheros normales del disco sin pasar por poll().
 */
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

// Página de error por defecto (el subject exige tenerlas si no se configuran)
std::string defaultErrorPage(int code)
{
	std::string status = toString(code) + " " + statusReason(code);
	return "<!DOCTYPE html>\n<html>\n<head><title>" + status + "</title></head>\n"
		   "<body>\n<center><h1>" + status + "</h1></center>\n"
		   "<hr><center>webserv</center>\n</body>\n</html>\n";
}
