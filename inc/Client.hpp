#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <ctime>
#include <string>
#include <vector>
#include "Cgi.hpp"
#include "HttpRequest.hpp"
#include "Server.hpp"

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
    int _fd;
    std::string _address;
    int _port;
    const std::vector<const Server*>& _servers;
    const Server* _server;
    State _state;
    HttpRequest _request;
    std::string _write_buffer;
    size_t _write_offset;
    bool _keep_alive;
    Cgi* _cgi;
    std::time_t _last_activity;

    Client(const Client&);
    Client& operator=(const Client&);

    void processInput();
    void dispatch();
    void queueResponse(const HttpResponse& response, bool keep_alive);
    void queueError(int code);
    void finishCgi(int error_code);

public:
    Client(int fd, const std::string& address, int port,
           const std::vector<const Server*>& servers);
    ~Client();

    int getFd() const;
    State getState() const;
    Cgi* getCgi() const;

    bool onReadable();
    bool onWritable();
    bool checkTimeout(std::time_t now);
    void onCgiWritable();
    void onCgiReadable();
};

#endif
