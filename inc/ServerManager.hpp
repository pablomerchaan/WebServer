#ifndef SERVER_MANAGER_HPP
#define SERVER_MANAGER_HPP

#include <map>
#include <string>
#include <vector>
#include <poll.h>
#include "Client.hpp"
#include "Server.hpp"

/*
 * ServerManager: dueño de todos los sockets y del bucle principal.
 *
 * El subject exige UN SOLO poll() para todo el I/O (sockets de escucha,
 * clientes y pipes de CGI) y que nunca se haga read/recv/write/send sin que
 * poll() haya dicho antes que ese fd está listo. Esta clase es la que cumple
 * esa regla: es el único sitio donde se llama a poll() y desde aquí se avisa
 * a cada Client de que puede leer o escribir.
 */
class ServerManager
{
private:
    /*
     * Un socket de escucha (host:puerto). Varios bloques "server" pueden
     * compartir el mismo host:puerto (virtual hosts): todos van en "servers"
     * y luego la cabecera Host decide cuál atiende cada petición.
     */
    struct Listener
    {
        int fd;
        std::string host;
        int port;
        std::vector<const Server*> servers;
    };

    /*
     * Qué hay detrás de cada entrada del array de pollfd. Se guarda en un
     * vector paralelo a _pollfds para saber a quién entregar cada evento.
     */
    enum EntryType
    {
        LISTENER,
        CLIENT,
        CGI_INPUT,
        CGI_OUTPUT
    };
    struct Entry
    {
        EntryType type;
        size_t listener;  // índice en _listeners (solo para LISTENER)
        Client* client;   // cliente dueño (para CLIENT, CGI_INPUT y CGI_OUTPUT)
    };

    const std::vector<Server>& _servers;
    std::vector<Listener> _listeners;
    std::map<int, Client*> _clients;     // fd del socket -> cliente
    std::vector<pollfd> _pollfds;
    std::vector<Entry> _entries;

    ServerManager(const ServerManager&);
    ServerManager& operator=(const ServerManager&);

    void openListener(Listener& listener);
    void buildPollList();
    void addPollEntry(int fd, short events, EntryType type, size_t listener, Client* client);
    void acceptClient(const Listener& listener);
    void closeClient(Client* client);
    void checkTimeouts();

public:
    explicit ServerManager(const std::vector<Server>& servers);
    ~ServerManager();

    // Crea y pone a escuchar todos los sockets (lanza excepción si falla)
    void setup();
    // Bucle principal: no vuelve hasta recibir SIGINT/SIGTERM
    void run();
};

#endif
