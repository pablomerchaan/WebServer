#ifndef SERVER_HPP
#define SERVER_HPP

#include <map>
#include <string>
#include <vector>
#include "Location.hpp"

class Server
{
private:
    int _port;
    std::string _host;
    std::string _server_name;
    std::string _root;
    std::string _index;
    unsigned long _client_max_body_size;
    std::map<int, std::string> _error_pages;
    std::vector<Location> _locations;

public:
    Server();

    void setPort(int port);
    void setHost(const std::string& host);
    void setServerName(const std::string& server_name);
    void setRoot(const std::string& root);
    void setIndex(const std::string& index);
    void setClientMaxBodySize(unsigned long size);
    void addErrorPage(int code, const std::string& path);
    void addLocation(const Location& location);

    int getPort() const;
    const std::string& getHost() const;
    const std::string& getServerName() const;
    const std::string& getRoot() const;
    const std::string& getIndex() const;
    unsigned long getClientMaxBodySize() const;
    const std::map<int, std::string>& getErrorPages() const;
    const std::vector<Location>& getLocations() const;
};

#endif
