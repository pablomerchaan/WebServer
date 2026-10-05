#include "../inc/ParseConfig.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

ParseConfig::ParseConfig() : _position(0) {}

std::string ParseConfig::readFile(const std::string& filename) const
{
	std::ifstream file(filename.c_str());
	if (!file)
		throw std::runtime_error("Cannot open configuration file: " + filename);
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

void ParseConfig::tokenize(const std::string& content)
{
	std::string token;
	bool comment = false;
	_tokens.clear();
	for (size_t i = 0; i < content.size(); ++i)
	{
		char character = content[i];
		if (comment)
		{
			if (character == '\n')
				comment = false;
			continue;
		}
		if (character == '#')
		{
			comment = true;
			continue;
		}
		if (character == '{' || character == '}' || character == ';' ||
			character == ' ' || character == '\n' || character == '\t' || character == '\r')
		{
			if (!token.empty())
			{
				_tokens.push_back(token);
				token.clear();
			}
			if (character == '{' || character == '}' || character == ';')
				_tokens.push_back(std::string(1, character));
		}
		else
			token += character;
	}
	if (!token.empty())
		_tokens.push_back(token);
}

bool ParseConfig::hasNext() const { return _position < _tokens.size(); }

std::string ParseConfig::nextToken()
{
	if (!hasNext())
		throw std::runtime_error("Unexpected end of configuration");
	return _tokens[_position++];
}

void ParseConfig::expect(const std::string& token)
{
	if (nextToken() != token)
		throw std::runtime_error("Expected '" + token + "'");
}

std::vector<std::string> ParseConfig::readValuesUntilSemicolon()
{
	std::vector<std::string> values;
	while (hasNext() && _tokens[_position] != ";")
	{
		if (_tokens[_position] == "{" || _tokens[_position] == "}")
			throw std::runtime_error("Expected ';' after directive");
		values.push_back(nextToken());
	}
	expect(";");
	if (values.empty())
		throw std::runtime_error("Directive has no value");
	return values;
}

int ParseConfig::parseInteger(const std::string& value, const std::string& directive) const
{
	char* end = NULL;
	long number = std::strtol(value.c_str(), &end, 10);
	if (value.empty() || *end != '\0' || number < 0 || number > 2147483647)
		throw std::runtime_error("Invalid value for " + directive);
	return static_cast<int>(number);
}

unsigned long ParseConfig::parseUnsignedLong(const std::string& value,
											 const std::string& directive) const
{
	char* end = NULL;
	unsigned long number = std::strtoul(value.c_str(), &end, 10);
	if (value.empty() || *end != '\0')
		throw std::runtime_error("Invalid value for " + directive);
	return number;
}

void ParseConfig::parseServerDirective(Server& server, const std::string& directive)
{
	std::vector<std::string> values = readValuesUntilSemicolon();
	if (directive == "listen" && values.size() == 1)
		server.setPort(parseInteger(values[0], directive));
	else if (directive == "host" && values.size() == 1)
		server.setHost(values[0]);
	else if (directive == "server_name" && values.size() == 1)
		server.setServerName(values[0]);
	else if (directive == "root" && values.size() == 1)
		server.setRoot(values[0]);
	else if (directive == "index" && values.size() == 1)
		server.setIndex(values[0]);
	else if (directive == "client_max_body_size" && values.size() == 1)
		server.setClientMaxBodySize(parseUnsignedLong(values[0], directive));
	else if (directive == "error_page" && values.size() == 2)
		server.addErrorPage(parseInteger(values[0], directive), values[1]);
	else
		throw std::runtime_error("Invalid or unknown server directive: " + directive);
}

void ParseConfig::parseLocationDirective(Location& location, const std::string& directive)
{
	std::vector<std::string> values = readValuesUntilSemicolon();
	if (directive == "root" && values.size() == 1)
		location.setRoot(values[0]);
	else if (directive == "index" && values.size() == 1)
		location.setIndex(values[0]);
	else if (directive == "allow_methods")
		location.setAllowedMethods(values);
	else if (directive == "autoindex" && values.size() == 1 &&
			 (values[0] == "on" || values[0] == "off"))
		location.setAutoindex(values[0] == "on");
	else if (directive == "return" && values.size() == 1)
		location.setReturn(values[0]);
	else if (directive == "alias" && values.size() == 1)
		location.setAlias(values[0]);
	else if (directive == "cgi_path")
		location.setCgiPaths(values);
	else if (directive == "cgi_ext")
		location.setCgiExtensions(values);
	else
		throw std::runtime_error("Invalid or unknown location directive: " + directive);
}

Location ParseConfig::parseLocation()
{
	Location location;
	location.setPath(nextToken());
	expect("{");
	while (hasNext() && _tokens[_position] != "}")
		parseLocationDirective(location, nextToken());
	expect("}");
	return location;
}

Server ParseConfig::parseServer()
{
	Server server;
	expect("server");
	expect("{");
	while (hasNext() && _tokens[_position] != "}")
	{
		std::string directive = nextToken();
		if (directive == "location")
			server.addLocation(parseLocation());
		else
			parseServerDirective(server, directive);
	}
	expect("}");
	if (server.getPort() == 0)
		throw std::runtime_error("Server has no valid listen directive");
	return server;
}

std::vector<Server> ParseConfig::parse(const std::string& filename)
{
	tokenize(readFile(filename));
	_position = 0;
	std::vector<Server> servers;
	while (hasNext())
		servers.push_back(parseServer());
	if (servers.empty())
		throw std::runtime_error("Configuration has no server block");
	return servers;
}