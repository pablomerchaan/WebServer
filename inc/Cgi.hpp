#ifndef CGI_HPP
#define CGI_HPP

#include <ctime>
#include <string>
#include <vector>
#include <sys/types.h>
#include "HttpResponse.hpp"

/*
 * Cgi: ejecuta un script CGI (Python, bash...) en un proceso hijo y recoge
 * su salida SIN BLOQUEAR el servidor.
 *
 *               pipe "input"                     pipe "output"
 *   servidor ──────────────────> stdin  [script]  stdout ──────────────────> servidor
 *   (escribe el body del POST)                    (lee cabeceras + HTML que genera)
 *
 * Los extremos del servidor (_input_fd y _output_fd) son no bloqueantes y el
 * ServerManager los mete en su poll(): solo se escribe/lee cuando poll() dice
 * que están listos, igual que con los sockets (requisito del subject).
 *
 * Ciclo de vida: start() -> writeInput()/readOutput() según poll ->
 * readOutput() devuelve false (EOF) -> buildResponse() -> destructor/stop().
 */
class Cgi
{
private:
    pid_t _pid;              // PID del proceso hijo (-1 si no hay)
    int _input_fd;           // escribimos aquí -> llega al stdin del script
    int _output_fd;          // leemos de aquí <- lo que el script escribe en stdout
    std::string _input;      // body de la petición que hay que mandarle
    size_t _input_offset;    // cuánto del body se ha enviado ya
    std::string _output;     // todo lo que ha escrito el script hasta ahora
    std::time_t _start_time; // para el timeout (scripts colgados/bucles infinitos)

    // No copiable: tiene fds y un proceso, copiarlo los cerraría dos veces
    Cgi(const Cgi&);
    Cgi& operator=(const Cgi&);

public:
    Cgi();
    ~Cgi();  // llama a stop(): nunca deja fds abiertos ni procesos zombi

    // Lanza el script. interpreter "" = ejecutarlo directamente (con su #!).
    // Devuelve false si no se pudo (sin pipes, fork falló...)
    bool start(const std::string& interpreter, const std::string& script,
               const std::vector<std::string>& env, const std::string& body);

    int getInputFd() const;   // -1 cuando ya se envió todo el body
    int getOutputFd() const;  // -1 cuando el script ya terminó su salida
    std::time_t getStartTime() const;

    // UNA llamada a write() por evento de poll; cierra la pipe al terminar
    void writeInput();
    // UNA llamada a read() por evento de poll; false = EOF (el script acabó)
    bool readOutput();
    void closeInput();
    // Cierra todo y recoge el proceso hijo (matándolo si sigue vivo)
    void stop();

    // Convierte la salida del script en una respuesta HTTP
    HttpResponse buildResponse() const;
};

#endif
