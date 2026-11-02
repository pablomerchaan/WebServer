#ifndef LOCATION_HPP
#define LOCATION_HPP

#include <string>
#include <vector>

class Location
{
private:
    std::string _path;
    std::string _root;
    std::string _index;
    std::vector<std::string> _allowed_methods;
    bool _autoindex;
    int _return_code;
    std::string _return;
    std::string _alias;
    std::vector<std::string> _cgi_paths;
    std::vector<std::string> _cgi_extensions;
    std::string _upload_store;
    unsigned long _client_max_body_size;
    bool _has_client_max_body_size;

public:
    Location();

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

    bool isMethodAllowed(const std::string& method) const;

    bool getCgiInterpreter(const std::string& extension, std::string& interpreter) const;
};

#endif
