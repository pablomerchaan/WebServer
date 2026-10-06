#ifndef LOCATION_HPP
#define LOCATION_HPP

#include <string>
#include <vector>

/*
 * Location: representa un bloque "location /ruta { ... }" del archivo de
 * configuración. Define cómo se tratan las peticiones cuya URI empieza por
 * esa ruta (métodos permitidos, raíz, autoindex, CGI, subidas...).
 *
 * Es una clase de datos: el parser (ParseConfig) la rellena con los setters
 * y el RequestHandler la consulta con los getters al atender una petición.
 */
class Location
{
private:
    std::string _path;                        // prefijo de URI: "/", "/tours", "/cgi-bin"...
    std::string _root;                        // "root": URI /x se busca en <root>/x (vacío = el del server)
    std::string _index;                       // fichero a servir si piden un directorio (vacío = el del server)
    std::vector<std::string> _allowed_methods;// "allow_methods" (vacío = todos los implementados)
    bool _autoindex;                          // "autoindex on": listar directorios sin index
    int _return_code;                         // código de "return" (0 = sin redirección)
    std::string _return;                      // URL destino de la redirección
    std::string _alias;                       // "alias": sustituye el prefijo de la location por esta ruta
    std::vector<std::string> _cgi_paths;      // "cgi_path": intérpretes (/usr/bin/python3, /bin/bash...)
    std::vector<std::string> _cgi_extensions; // "cgi_ext": extensiones (.py, .sh...) emparejadas con cgi_path
    std::string _upload_store;                // "upload_store": carpeta donde se guardan las subidas (POST)
    unsigned long _client_max_body_size;      // límite de body propio de esta location
    bool _has_client_max_body_size;           // true si se definió (si no, se usa el del server)

public:
    Location();

    /* --- Setters: los usa el parser al leer el archivo de configuración --- */
    void setPath(const std::string& path);
    void setRoot(const std::string& root);
    void setIndex(const std::string& index);
    void setAllowedMethods(const std::vector<std::string>& methods);
    void setAutoindex(bool autoindex);
    void setReturn(int code, const std::string& value);
    void setAlias(const std::string& alias);
    void setCgiPaths(const std::vector<std::string>& paths);
    void setCgiExtensions(const std::vector<std::string>& extensions);
    void setUploadStore(const std::string& path);
    void setClientMaxBodySize(unsigned long size);

    /* --- Getters: los usa el RequestHandler al procesar peticiones --- */
    const std::string& getPath() const;
    const std::string& getRoot() const;
    const std::string& getIndex() const;
    const std::vector<std::string>& getAllowedMethods() const;
    bool getAutoindex() const;
    int getReturnCode() const;
    const std::string& getReturn() const;
    const std::string& getAlias() const;
    const std::vector<std::string>& getCgiPaths() const;
    const std::vector<std::string>& getCgiExtensions() const;
    const std::string& getUploadStore() const;
    unsigned long getClientMaxBodySize() const;
    bool hasClientMaxBodySize() const;

    // ¿Se puede usar este método HTTP (GET, POST...) en esta location?
    bool isMethodAllowed(const std::string& method) const;

    // Si la extensión (".py") está en cgi_ext, devuelve true y pone en
    // "interpreter" el programa que la ejecuta ("" = ejecutar el script
    // directamente). Devuelve false si esa extensión no es CGI aquí.
    bool getCgiInterpreter(const std::string& extension, std::string& interpreter) const;
};

#endif
