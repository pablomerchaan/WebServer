#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <ctime>
#include <string>
#include <vector>
#include "Cgi.hpp"
#include "HttpRequest.hpp"
#include "Server.hpp"

/*
 * Client: una conexión TCP abierta con un navegador/cliente.
 *
 * Cada conexión pasa por estos estados (y vuelve a empezar con keep-alive):
 *
 *        ┌──────────────────────────────────────────────────┐
 *        v                                                  │ keep-alive
 *    READING ──(petición completa)──> WRITING ──(enviado)───┘
 *        │                               ^          └──(Connection: close)──> cerrar
 *        └──(es un CGI)──> CGI ──(EOF)───┘
 *
 *   READING: poll vigila POLLIN del socket; se van acumulando bytes en _request
 *   CGI:     poll vigila las pipes del script, no el socket
 *   WRITING: poll vigila POLLOUT del socket; se envía _write_buffer a trozos
 *
 * Los métodos on*() los llama el ServerManager cuando poll() avisa de que un
 * fd está listo. Cada uno hace UNA sola operación de lectura/escritura.
 */
class Client
{
public:
    enum State
    {
        READING,
        CGI,
        WRITING
    };

private:
    int _fd;                                   // socket de la conexión
    std::string _address;                      // IP del cliente
    int _port;                                 // puerto local por el que conectó
    const std::vector<const Server*>& _servers;// servers que escuchan en ese host:puerto
    const Server* _server;                     // el elegido para la petición actual
    State _state;
    HttpRequest _request;                      // petición en curso (parser incremental)
    std::string _write_buffer;                 // respuesta ya serializada
    size_t _write_offset;                      // cuánto se ha enviado ya
    bool _keep_alive;                          // ¿seguir abierta tras responder?
    Cgi* _cgi;                                 // CGI en ejecución (NULL si no hay)
    std::time_t _last_activity;                // para el timeout de inactividad

    // No copiable: posee un socket y un CGI
    Client(const Client&);
    Client& operator=(const Client&);

    void processInput();   // hacer avanzar el parser y actuar según su estado
    void dispatch();       // petición completa -> RequestHandler
    void queueResponse(const HttpResponse& response, bool keep_alive);
    void queueError(int code);
    void finishCgi(int error_code);

public:
    Client(int fd, const std::string& address, int port,
           const std::vector<const Server*>& servers);
    ~Client();  // cierra el socket y para el CGI si lo hay

    int getFd() const;
    State getState() const;
    Cgi* getCgi() const;

    // Devuelven false cuando hay que cerrar la conexión
    bool onReadable();                 // socket con datos -> un recv()
    bool onWritable();                 // socket con hueco -> un send()
    bool checkTimeout(std::time_t now);
    void onCgiWritable();              // pipe stdin del CGI con hueco -> un write()
    void onCgiReadable();              // pipe stdout del CGI con datos -> un read()
};

#endif
