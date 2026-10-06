#ifndef REQUEST_HANDLER_HPP
#define REQUEST_HANDLER_HPP

#include <string>
#include <vector>
#include "Cgi.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "Server.hpp"

/*
 * RequestHandler: decide QUÉ responder a una petición ya completa.
 * No sabe nada de sockets ni de poll(): recibe un HttpRequest y devuelve un
 * HttpResponse (o lanza un CGI). Se crea uno por petición.
 *
 * Orden de decisiones en handle():
 *   1. ¿Método implementado?              no -> 501
 *   2. ¿Método permitido en la location?  no -> 405 (+ cabecera Allow)
 *   3. ¿La location tiene "return"?       sí -> redirección 30x
 *   4. ¿Es un script CGI?                 sí -> lanzar CGI
 *   5. GET/HEAD -> fichero / index / autoindex
 *      POST     -> subida de ficheros
 *      DELETE   -> borrar fichero
 */
class RequestHandler
{
private:
    const Server& _server;         // server elegido para esta petición
    const HttpRequest& _request;
    std::string _client_address;   // IP del cliente (REMOTE_ADDR para el CGI)
    int _port;                     // puerto por el que llegó (SERVER_PORT)
    Location _fallback;            // location por defecto si ninguna coincide
    const Location* _location;     // location que aplica a esta URI

    // URI -> ruta en disco ("/tours/a.html" -> "www/tours/a.html")
    std::string mapPath(const std::string& uri_path) const;
    // Nombre del fichero index aplicable (el de la location o el del server)
    std::string indexName() const;
    // Busca un script CGI dentro de la URI y separa el PATH_INFO
    bool findCgiScript(const std::string& uri_path, std::string& script_uri,
                       std::string& path_info) const;

    HttpResponse handleGet(const std::string& path);
    HttpResponse handlePost(const std::string& path);
    HttpResponse handleDelete(const std::string& path);
    HttpResponse serveFile(const std::string& path);
    HttpResponse autoindex(const std::string& path);
    HttpResponse redirect(int code, const std::string& location);
    HttpResponse saveUploads(const std::string& directory);
    bool startCgi(const std::string& script_uri, const std::string& path_info, Cgi*& cgi);

public:
    RequestHandler(const Server& server, const HttpRequest& request,
                   const std::string& client_address, int port);

    // Devuelve la respuesta. Si en vez de eso lanza un CGI, pone "cgi" a un
    // objeto nuevo (y la respuesta devuelta no se usa): la respuesta real
    // llegará cuando el script termine.
    HttpResponse handle(Cgi*& cgi);

    // Elige, entre los servers de un mismo host:puerto, el que corresponde
    // a la cabecera Host (o el primero si ninguno coincide)
    static const Server& selectServer(const std::vector<const Server*>& servers,
                                      const HttpRequest& request);
    // client_max_body_size aplicable (el de la location si lo define)
    static unsigned long maxBodySize(const Server& server, const HttpRequest& request);
    // Respuesta de error con la página de error_page o la de por defecto
    static HttpResponse errorResponse(const Server& server, int code);
};

#endif
