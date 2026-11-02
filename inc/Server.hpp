#ifndef SERVER_HPP
#define SERVER_HPP

#include <map>
#include <string>
#include <vector>
#include "Location.hpp"

class Server
{
private:
    std::vector<int> _ports;
    std::string _host;
    std::vector<std::string> _server_names;
    std::string _root;
    std::string _index;
    unsigned long _client_max_body_size;
    std::map<int, std::string> _error_pages;
    std::vector<Location> _locations;

public:
    Server();

    void addPort(int port);
    void setHost(const std::string& host);
    void setServerNames(const std::vector<std::string>& server_names);
    void setRoot(const std::string& root);
    void setIndex(const std::string& index);
    void setClientMaxBodySize(unsigned long size);
    void addErrorPage(int code, const std::string& path);
    void addLocation(const Location& location);

    const std::vector<int>& getPorts() const;
    const std::string& getHost() const;
    const std::vector<std::string>& getServerNames() const;
    const std::string& getRoot() const;
    const std::string& getIndex() const;
    unsigned long getClientMaxBodySize() const;
    const std::map<int, std::string>& getErrorPages() const;
    const std::vector<Location>& getLocations() const;

    bool hasServerName(const std::string& name) const;

    const Location* matchLocation(const std::string& path) const;
};

#endif
