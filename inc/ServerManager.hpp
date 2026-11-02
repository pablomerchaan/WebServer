#ifndef SERVER_MANAGER_HPP
#define SERVER_MANAGER_HPP

#include <map>
#include <string>
#include <vector>
#include <poll.h>
#include "Client.hpp"
#include "Server.hpp"

class ServerManager
{
private:

    struct Listener
    {
        int fd;
        std::string host;
        int port;
        std::vector<const Server*> servers;
    };

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
        size_t listener;
        Client* client;
    };

    const std::vector<Server>& _servers;
    std::vector<Listener> _listeners;
    std::map<int, Client*> _clients;
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

    void setup();

    void run();
};

#endif
