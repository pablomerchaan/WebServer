#ifndef SERVER_HPP
#define SERVER_HPP

#include <map>
#include <string>
#include <vector>
#include "Location.hpp"

/*
 * Server: representa un bloque "server { ... }" del archivo de configuración,
 * es decir, un "servidor virtual": puertos en los que escucha, nombres, raíz,
 * páginas de error y sus bloques location.
 *
 * Igual que Location, es una clase de datos rellenada por ParseConfig.
 * Ojo: NO es el que abre sockets; eso lo hace ServerManager.
 */
class Server
{
private:
    std::vector<int> _ports;                 // "listen": puede haber varios
    std::string _host;                       // "host": IP/nombre donde escuchar (por defecto 0.0.0.0 = todas)
    std::vector<std::string> _server_names;  // "server_name": para elegir server con la cabecera Host
    std::string _root;                       // "root": carpeta raíz de los ficheros
    std::string _index;                      // "index": fichero por defecto de un directorio
    unsigned long _client_max_body_size;     // tamaño máximo del body de una petición (0 = sin límite)
    std::map<int, std::string> _error_pages; // "error_page": código de error -> página personalizada
    std::vector<Location> _locations;        // bloques "location" de este server

public:
    Server();

    /* --- Setters (los usa el parser) --- */
    void addPort(int port);
    void setHost(const std::string& host);
    void setServerNames(const std::vector<std::string>& server_names);
    void setRoot(const std::string& root);
    void setIndex(const std::string& index);
    void setClientMaxBodySize(unsigned long size);
    void addErrorPage(int code, const std::string& path);
    void addLocation(const Location& location);

    /* --- Getters --- */
    const std::vector<int>& getPorts() const;
    const std::string& getHost() const;
    const std::vector<std::string>& getServerNames() const;
    const std::string& getRoot() const;
    const std::string& getIndex() const;
    unsigned long getClientMaxBodySize() const;
    const std::map<int, std::string>& getErrorPages() const;
    const std::vector<Location>& getLocations() const;

    // ¿Tiene este server el nombre "name" en su server_name?
    bool hasServerName(const std::string& name) const;

    // Busca la location que corresponde a una URI usando "el prefijo más
    // largo" (como nginx). Devuelve NULL si ninguna coincide.
    const Location* matchLocation(const std::string& path) const;
};

#endif
