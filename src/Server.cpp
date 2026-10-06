#include "../inc/Server.hpp"

/*
 * Valores por defecto si la config no dice nada:
 *  - host 0.0.0.0     -> escuchar en todas las interfaces de red
 *  - index index.html
 *  - client_max_body_size 1 MiB (el mismo valor por defecto que nginx)
 */
Server::Server() : _host("0.0.0.0"), _index("index.html"), _client_max_body_size(1048576)
{
}

/* ======================================================================
 *  Setters
 * ====================================================================== */

void Server::addPort(int port) { _ports.push_back(port); }
void Server::setHost(const std::string& host) { _host = host; }
void Server::setServerNames(const std::vector<std::string>& server_names) { _server_names = server_names; }
void Server::setRoot(const std::string& root) { _root = root; }
void Server::setIndex(const std::string& index) { _index = index; }
void Server::setClientMaxBodySize(unsigned long size) { _client_max_body_size = size; }
void Server::addErrorPage(int code, const std::string& path) { _error_pages[code] = path; }
void Server::addLocation(const Location& location) { _locations.push_back(location); }

/* ======================================================================
 *  Getters
 * ====================================================================== */

const std::vector<int>& Server::getPorts() const { return _ports; }
const std::string& Server::getHost() const { return _host; }
const std::vector<std::string>& Server::getServerNames() const { return _server_names; }
const std::string& Server::getRoot() const { return _root; }
const std::string& Server::getIndex() const { return _index; }
unsigned long Server::getClientMaxBodySize() const { return _client_max_body_size; }
const std::map<int, std::string>& Server::getErrorPages() const { return _error_pages; }
const std::vector<Location>& Server::getLocations() const { return _locations; }

/* ======================================================================
 *  Consultas
 * ====================================================================== */

bool Server::hasServerName(const std::string& name) const
{
    for (size_t i = 0; i < _server_names.size(); ++i)
        if (_server_names[i] == name)
            return true;
    return false;
}

/*
 * Ejemplo con las locations "/", "/tours" y "/cgi-bin":
 *   "/tours/a.html" -> coinciden "/" y "/tours" -> gana "/tours" (más largo)
 *   "/style.css"    -> solo coincide "/"
 *   "/toursXYZ"     -> NO es "/tours" (no hay '/' tras el prefijo) -> "/"
 */
const Location* Server::matchLocation(const std::string& path) const
{
    const Location* best = NULL;
    size_t best_length = 0;
    for (size_t i = 0; i < _locations.size(); ++i)
    {
        const std::string& prefix = _locations[i].getPath();
        // ¿La URI empieza por el prefijo de esta location?
        if (path.compare(0, prefix.size(), prefix) != 0)
            continue;
        // El prefijo debe acabar en un límite de segmento: "/tours" vale para
        // "/tours" y "/tours/x" pero no para "/toursfoo"
        if (prefix != "/" && prefix[prefix.size() - 1] != '/' &&
            path.size() > prefix.size() && path[prefix.size()] != '/')
            continue;
        // Nos quedamos con la coincidencia más larga (la más específica)
        if (best == NULL || prefix.size() > best_length)
        {
            best = &_locations[i];
            best_length = prefix.size();
        }
    }
    return best;
}
