#ifndef HTTP_RESPONSE_HPP
#define HTTP_RESPONSE_HPP

#include <map>
#include <string>

/*
 * HttpResponse: una respuesta HTTP en construcción. Se rellena el código,
 * las cabeceras y el cuerpo, y build() lo convierte en el texto exacto que
 * se envía por el socket:
 *
 *     HTTP/1.1 200 OK\r\n                <- línea de estado
 *     Content-Type: text/html\r\n        <- cabeceras "Nombre: valor"
 *     Content-Length: 13\r\n
 *     \r\n                               <- línea vacía = fin de cabeceras
 *     <h1>Hola</h1>                      <- cuerpo
 */
class HttpResponse
{
private:
    int _status;                                  // código: 200, 404...
    std::map<std::string, std::string> _headers;  // cabeceras extra
    std::string _body;                            // contenido

public:
    HttpResponse();  // por defecto: 200 OK sin cuerpo

    void setStatus(int status);
    void setHeader(const std::string& name, const std::string& value);
    // Pone el cuerpo y su Content-Type de una vez
    void setBody(const std::string& body, const std::string& content_type);
    void setBody(const std::string& body);

    int getStatus() const;
    bool hasHeader(const std::string& name) const;

    // Genera el texto de la respuesta. keep_alive decide la cabecera
    // "Connection"; include_body = false para HEAD (cabeceras sin cuerpo)
    std::string build(bool keep_alive, bool include_body) const;
};

#endif
