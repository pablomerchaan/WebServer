#include "../inc/Location.hpp"

Location::Location() : _autoindex(false)
{
}

void Location::setPath(const std::string& path) { _path = path; }
void Location::setRoot(const std::string& root) { _root = root; }
void Location::setIndex(const std::string& index) { _index = index; }
void Location::setAllowedMethods(const std::vector<std::string>& methods) { _allowed_methods = methods; }
void Location::setAutoindex(bool autoindex) { _autoindex = autoindex; }
void Location::setReturn(const std::string& value) { _return = value; }
void Location::setAlias(const std::string& alias) { _alias = alias; }
void Location::setCgiPaths(const std::vector<std::string>& paths) { _cgi_paths = paths; }
void Location::setCgiExtensions(const std::vector<std::string>& extensions) { _cgi_extensions = extensions; }

const std::string& Location::getPath() const { return _path; }
const std::string& Location::getRoot() const { return _root; }
const std::string& Location::getIndex() const { return _index; }
const std::vector<std::string>& Location::getAllowedMethods() const { return _allowed_methods; }
bool Location::getAutoindex() const { return _autoindex; }
const std::string& Location::getReturn() const { return _return; }
const std::string& Location::getAlias() const { return _alias; }
const std::vector<std::string>& Location::getCgiPaths() const { return _cgi_paths; }
const std::vector<std::string>& Location::getCgiExtensions() const { return _cgi_extensions; }
