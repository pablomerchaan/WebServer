#ifndef UTILS_HPP
#define UTILS_HPP

#include <sstream>
#include <string>

/*
 * Funciones de ayuda que usan varias clases. No tienen estado: son simples
 * transformaciones de strings y consultas al sistema de ficheros.
 */

// Convierte cualquier valor imprimible a string (C++98 no tiene std::to_string)
template <typename T>
std::string toString(const T& value)
{
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

/* --- Strings --- */
std::string toLower(const std::string& str);                          // "Content-Type" -> "content-type"
std::string trim(const std::string& str);                             // quita espacios/tabs/saltos a los lados
bool endsWith(const std::string& str, const std::string& suffix);
std::string joinPath(const std::string& base, const std::string& path); // "www" + "/a" -> "www/a" (sin "//")
std::string getExtension(const std::string& path);                    // "/x/a.py" -> ".py"

/* --- HTTP --- */
std::string statusReason(int code);       // 404 -> "Not Found"
std::string mimeType(const std::string& path); // "a.css" -> "text/css"
std::string httpDate();                   // fecha actual en formato HTTP (cabecera Date)

// Decodifica secuencias %XX de una URL ("a%20b" -> "a b"). false si está mal formada
bool urlDecode(const std::string& in, std::string& out);
// Resuelve "." y ".." ("/a/./b/../c" -> "/a/c"). false si intenta salir de la raíz
// ("/../etc/passwd"): es la defensa contra path traversal
bool normalizePath(const std::string& in, std::string& out);
// Escapa < > & " para meter texto de forma segura dentro de HTML
std::string htmlEscape(const std::string& str);

/* --- Sistema de ficheros --- */
bool isDirectory(const std::string& path);
bool isRegularFile(const std::string& path);
bool readFile(const std::string& path, std::string& content);  // fichero entero -> string
std::string defaultErrorPage(int code);  // HTML genérico para un código de error

#endif
