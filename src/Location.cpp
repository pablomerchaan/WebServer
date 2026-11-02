#include "../inc/Location.hpp"

Location::Location()
    : _autoindex(false), _return_code(0), _client_max_body_size(0),
      _has_client_max_body_size(false)
{
}

void Location::setPath(const std::string& path) { _path = path; }
void Location::setRoot(const std::string& root) { _root = root; }
void Location::setIndex(const std::string& index) { _index = index; }
void Location::setAllowedMethods(const std::vector<std::string>& methods) { _allowed_methods = methods; }
void Location::setAutoindex(bool autoindex) { _autoindex = autoindex; }
void Location::setReturn(int code, const std::string& value) { _return_code = code; _return = value; }
void Location::setAlias(const std::string& alias) { _alias = alias; }
void Location::setCgiPaths(const std::vector<std::string>& paths) { _cgi_paths = paths; }
void Location::setCgiExtensions(const std::vector<std::string>& extensions) { _cgi_extensions = extensions; }
void Location::setUploadStore(const std::string& path) { _upload_store = path; }

void Location::setClientMaxBodySize(unsigned long size)
{
    _client_max_body_size = size;
    _has_client_max_body_size = true;
}

const std::string& Location::getPath() const { return _path; }
const std::string& Location::getRoot() const { return _root; }
const std::string& Location::getIndex() const { return _index; }
const std::vector<std::string>& Location::getAllowedMethods() const { return _allowed_methods; }
bool Location::getAutoindex() const { return _autoindex; }
int Location::getReturnCode() const { return _return_code; }
const std::string& Location::getReturn() const { return _return; }
const std::string& Location::getAlias() const { return _alias; }
const std::vector<std::string>& Location::getCgiPaths() const { return _cgi_paths; }
const std::vector<std::string>& Location::getCgiExtensions() const { return _cgi_extensions; }
const std::string& Location::getUploadStore() const { return _upload_store; }
unsigned long Location::getClientMaxBodySize() const { return _client_max_body_size; }
bool Location::hasClientMaxBodySize() const { return _has_client_max_body_size; }

bool Location::isMethodAllowed(const std::string& method) const
{

    if (_allowed_methods.empty())
        return true;

    for (size_t i = 0; i < _allowed_methods.size(); ++i)
        if (_allowed_methods[i] == method)
            return true;
    return false;
}

bool Location::getCgiInterpreter(const std::string& extension, std::string& interpreter) const
{
    for (size_t i = 0; i < _cgi_extensions.size(); ++i)
    {
        if (_cgi_extensions[i] != extension)
            continue;
        if (i < _cgi_paths.size())
            interpreter = _cgi_paths[i];
        else if (_cgi_paths.size() == 1)
            interpreter = _cgi_paths[0];
        else
            interpreter = "";
        return true;
    }
    return false;
}
