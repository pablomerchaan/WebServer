#ifndef UTILS_HPP
#define UTILS_HPP

#include <sstream>
#include <string>

template <typename T>
std::string toString(const T& value)
{
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

std::string toLower(const std::string& str);
std::string trim(const std::string& str);
bool endsWith(const std::string& str, const std::string& suffix);
std::string joinPath(const std::string& base, const std::string& path);
std::string getExtension(const std::string& path);

std::string statusReason(int code);
std::string mimeType(const std::string& path);
std::string httpDate();

bool urlDecode(const std::string& in, std::string& out);

bool normalizePath(const std::string& in, std::string& out);

std::string htmlEscape(const std::string& str);

bool isDirectory(const std::string& path);
bool isRegularFile(const std::string& path);
bool readFile(const std::string& path, std::string& content);
std::string defaultErrorPage(int code);

#endif
