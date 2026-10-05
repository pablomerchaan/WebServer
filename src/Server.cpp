#include "../inc/Server.hpp"

Server::Server() : _port(0), _client_max_body_size(0)
{
}

void Server::setPort(int port) { _port = port; }
void Server::setHost(const std::string& host) { _host = host; }
void Server::setServerName(const std::string& server_name) { _server_name = server_name; }
void Server::setRoot(const std::string& root) { _root = root; }
void Server::setIndex(const std::string& index) { _index = index; }
void Server::setClientMaxBodySize(unsigned long size) { _client_max_body_size = size; }
void Server::addErrorPage(int code, const std::string& path) { _error_pages[code] = path; }
void Server::addLocation(const Location& location) { _locations.push_back(location); }

int Server::getPort() const { return _port; }
const std::string& Server::getHost() const { return _host; }
const std::string& Server::getServerName() const { return _server_name; }
const std::string& Server::getRoot() const { return _root; }
const std::string& Server::getIndex() const { return _index; }
unsigned long Server::getClientMaxBodySize() const { return _client_max_body_size; }
const std::map<int, std::string>& Server::getErrorPages() const { return _error_pages; }
const std::vector<Location>& Server::getLocations() const { return _locations; }