#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include <map>
#include <string>

/*
 * HttpRequest: parser INCREMENTAL de peticiones HTTP/1.x.
 *
 * Con sockets no bloqueantes, cada recv() puede traer un trozo cualquiera:
 * media cabecera, la cabecera y parte del body, o varias peticiones seguidas.
 * Por eso no podemos "leer la petición entera de golpe". En su lugar:
 *
 *   1) append() añade los bytes recibidos a un buffer interno.
 *   2) parse() avanza todo lo que pueda y devuelve en qué estado se queda.
 *      Si faltan datos, simplemente se queda esperando al siguiente append().
 *
 * Máquina de estados:
 *
 *   HEADERS ──(llega "\r\n\r\n")──> HEADERS_DONE
 *                                       │  setMaxBodySize()
 *          ┌────────────────────────────┼─────────────────────────┐
 *          v                            v                         v
 *     sin body                BODY (Content-Length)     CHUNK_SIZE <-> CHUNK_DATA
 *          │                            │                         │ (chunk de tamaño 0)
 *          │                            │                    CHUNK_TRAILER
 *          └────────────> COMPLETE <────┴─────────────────────────┘
 *
 *   Cualquier error -> ERROR, con el código HTTP a devolver en _error_code.
 *
 * HEADERS_DONE es una pausa a propósito: el límite de tamaño del body
 * (client_max_body_size) depende del server y de la location, y eso solo se
 * sabe después de leer la cabecera Host y la URI. El Client lo calcula y
 * llama a setMaxBodySize(), que reanuda el parseo. Así podemos rechazar un
 * body demasiado grande (413) sin tener que recibirlo entero.
 */
class HttpRequest
{
public:
    enum State
    {
        HEADERS,        // esperando/leyendo la línea de petición y las cabeceras
        HEADERS_DONE,   // cabeceras leídas; esperando a que nos digan el límite del body
        BODY,           // leyendo un body de tamaño conocido (Content-Length)
        CHUNK_SIZE,     // chunked: esperando la línea con el tamaño (en hex) del chunk
        CHUNK_DATA,     // chunked: esperando los datos del chunk + "\r\n"
        CHUNK_TRAILER,  // chunked: tras el chunk 0, esperando la línea vacía final
        COMPLETE,       // petición completa y válida
        ERROR           // petición inválida (ver getErrorCode())
    };

private:
    State _state;
    std::string _buffer;   // bytes recibidos aún no procesados
    std::string _method;   // "GET", "POST"...
    std::string _target;   // URI tal cual llegó: "/a%20b/c.py?x=1"
    std::string _path;     // URI decodificada y normalizada, sin query: "/a b/c.py"
    std::string _query;    // lo que va tras '?': "x=1"
    std::string _version;  // "HTTP/1.1" o "HTTP/1.0"
    std::map<std::string, std::string> _headers;  // nombres en minúscula
    std::string _body;     // body ya "des-chunkeado"
    size_t _content_length;   // valor de Content-Length
    size_t _chunk_remaining;  // bytes del chunk actual
    unsigned long _max_body_size;  // límite (0 = sin límite)
    int _error_code;          // código HTTP si _state == ERROR

    State fail(int code);     // pasa a ERROR con ese código y devuelve el estado
    bool reject(int code);    // igual que fail() pero devuelve false (para funciones bool)
    bool parseHeaderSection(const std::string& section);
    bool parseRequestLine(const std::string& line);
    bool parseTarget();
    void parseBody();
    void parseChunks();

public:
    HttpRequest();

    // Añade bytes recibidos del socket al buffer
    void append(const char* data, size_t size);
    // Procesa todo lo posible del buffer y devuelve el estado resultante
    State parse();
    // Se llama una vez conocido el server/location: fija el límite y sigue parseando
    void setMaxBodySize(unsigned long size);
    // Prepara el objeto para la siguiente petición de la misma conexión
    // (keep-alive), conservando los bytes que ya hayan llegado de ella
    void reset();

    State getState() const;
    int getErrorCode() const;
    const std::string& getMethod() const;
    const std::string& getTarget() const;
    const std::string& getPath() const;
    const std::string& getQuery() const;
    const std::string& getVersion() const;
    const std::map<std::string, std::string>& getHeaders() const;
    std::string getHeader(const std::string& name) const;  // "" si no existe
    const std::string& getBody() const;
    bool hasBufferedData() const;  // ¿quedan bytes sin procesar?
    bool keepAlive() const;        // ¿mantener la conexión abierta tras responder?
};

#endif
