#include "../inc/ParseConfig.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

/*
 * El parseo se hace en dos fases:
 *
 *  1) TOKENIZAR: el texto se trocea en "palabras" (tokens). Los caracteres
 *     '{', '}' y ';' son tokens por sí mismos; los espacios separan tokens;
 *     y desde '#' hasta fin de línea es comentario. Por ejemplo:
 *
 *         server { listen 8002; }   ->   [server] [{] [listen] [8002] [;] [}]
 *
 *  2) PARSEAR: se recorren los tokens con un índice (_position) siguiendo la
 *     gramática:  server { directiva valores... ; location /ruta { ... } }
 *     Cualquier cosa inesperada lanza una excepción con un mensaje claro, y
 *     main() la muestra y termina (no arrancamos con una config inválida).
 */

ParseConfig::ParseConfig() : _position(0) {}

// Carga el archivo completo en un string
std::string ParseConfig::readFile(const std::string& filename) const
{
	std::ifstream file(filename.c_str());
	if (!file)
		throw std::runtime_error("Cannot open configuration file: " + filename);
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

/* ======================================================================
 *  Fase 1: tokenizador
 * ====================================================================== */

void ParseConfig::tokenize(const std::string& content)
{
	std::string token;     // palabra que estamos acumulando
	bool comment = false;  // true mientras estamos dentro de un comentario
	_tokens.clear();
	for (size_t i = 0; i < content.size(); ++i)
	{
		char character = content[i];
		// Dentro de un comentario ignoramos todo hasta el salto de línea
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
		// Separadores: cierran la palabra en curso. Las llaves y el punto y
		// coma además se guardan como token propio.
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
	// Última palabra si el archivo no acaba en separador
	if (!token.empty())
		_tokens.push_back(token);
}

/* ======================================================================
 *  Herramientas para recorrer los tokens
 * ====================================================================== */

// ¿Quedan tokens por leer?
bool ParseConfig::hasNext() const { return _position < _tokens.size(); }

// Devuelve el token actual y avanza; error si el archivo se acaba antes de tiempo
std::string ParseConfig::nextToken()
{
	if (!hasNext())
		throw std::runtime_error("Unexpected end of configuration");
	return _tokens[_position++];
}

// Exige que el siguiente token sea exactamente "token" (p.ej. "{" o ";")
void ParseConfig::expect(const std::string& token)
{
	if (nextToken() != token)
		throw std::runtime_error("Expected '" + token + "'");
}

/*
 * Lee los valores de una directiva hasta el ';'.
 * Ej: tras leer "error_page", con tokens [404] [/404.html] [;]
 * devuelve {"404", "/404.html"} y consume el ';'.
 */
std::vector<std::string> ParseConfig::readValuesUntilSemicolon()
{
	std::vector<std::string> values;
	while (hasNext() && _tokens[_position] != ";")
	{
		// Encontrar una llave aquí significa que falta el ';'
		if (_tokens[_position] == "{" || _tokens[_position] == "}")
			throw std::runtime_error("Expected ';' after directive");
		values.push_back(nextToken());
	}
	expect(";");
	if (values.empty())
		throw std::runtime_error("Directive has no value");
	return values;
}

/* ======================================================================
 *  Conversión y validación de números
 * ====================================================================== */

// Entero entre 0 e INT_MAX; rechaza basura como "80abc"
int ParseConfig::parseInteger(const std::string& value, const std::string& directive) const
{
	char* end = NULL;
	long number = std::strtol(value.c_str(), &end, 10);
	// *end != '\0' significa que strtol se paró antes del final: había letras
	if (value.empty() || *end != '\0' || number < 0 || number > 2147483647)
		throw std::runtime_error("Invalid value for " + directive);
	return static_cast<int>(number);
}

/*
 * Tamaño en bytes con sufijo opcional estilo nginx:
 *   "100" -> 100    "10k" -> 10240    "8m" -> 8388608    "1g" -> 1073741824
 */
unsigned long ParseConfig::parseUnsignedLong(const std::string& value,
											 const std::string& directive) const
{
	char* end = NULL;
	// strtoul acepta "-5" (dando un número enorme), así que lo rechazamos antes
	if (value.empty() || value[0] == '-')
		throw std::runtime_error("Invalid value for " + directive);
	unsigned long number = std::strtoul(value.c_str(), &end, 10);
	if (end == value.c_str())  // no había ningún dígito
		throw std::runtime_error("Invalid value for " + directive);
	unsigned long multiplier = 1;
	if (*end == 'k' || *end == 'K')
		multiplier = 1024UL;
	else if (*end == 'm' || *end == 'M')
		multiplier = 1024UL * 1024UL;
	else if (*end == 'g' || *end == 'G')
		multiplier = 1024UL * 1024UL * 1024UL;
	if (multiplier != 1)
		++end;  // saltamos la letra del sufijo
	// Después del número (y su sufijo) no puede haber nada más
	if (*end != '\0')
		throw std::runtime_error("Invalid value for " + directive);
	return number * multiplier;
}

// Un puerto TCP válido va de 1 a 65535
int ParseConfig::parsePort(const std::string& value) const
{
	int port = parseInteger(value, "listen");
	if (port < 1 || port > 65535)
		throw std::runtime_error("Invalid port in listen: " + value);
	return port;
}

/* ======================================================================
 *  Fase 2: directivas
 * ====================================================================== */

/*
 * Directivas permitidas dentro de "server { }". "directive" es el nombre
 * (ya leído) y aquí se leen sus valores y se guardan en el Server.
 * Cada directiva comprueba también cuántos valores recibe.
 */
void ParseConfig::parseServerDirective(Server& server, const std::string& directive)
{
	std::vector<std::string> values = readValuesUntilSemicolon();
	if (directive == "listen" && values.size() == 1)
	{
		// Dos formas: "listen 8080;" o "listen 127.0.0.1:8080;"
		size_t colon = values[0].find(':');
		if (colon != std::string::npos)
		{
			server.setHost(values[0].substr(0, colon));
			server.addPort(parsePort(values[0].substr(colon + 1)));
		}
		else
			server.addPort(parsePort(values[0]));
	}
	else if (directive == "host" && values.size() == 1)
		server.setHost(values[0]);
	else if (directive == "server_name")  // admite varios nombres
		server.setServerNames(values);
	else if (directive == "root" && values.size() == 1)
		server.setRoot(values[0]);
	else if (directive == "index" && values.size() == 1)
		server.setIndex(values[0]);
	else if (directive == "client_max_body_size" && values.size() == 1)
		server.setClientMaxBodySize(parseUnsignedLong(values[0], directive));
	else if (directive == "error_page" && values.size() >= 2)
	{
		// "error_page 500 502 503 /50x.html;" -> todos los códigos menos el
		// último valor, que es la página que se usará para ellos
		for (size_t i = 0; i + 1 < values.size(); ++i)
		{
			int code = parseInteger(values[i], directive);
			if (code < 300 || code > 599)
				throw std::runtime_error("Invalid status code in error_page: " + values[i]);
			server.addErrorPage(code, values.back());
		}
	}
	else
		throw std::runtime_error("Invalid or unknown server directive: " + directive);
}

// Directivas permitidas dentro de "location /ruta { }"
void ParseConfig::parseLocationDirective(Location& location, const std::string& directive)
{
	std::vector<std::string> values = readValuesUntilSemicolon();
	if (directive == "root" && values.size() == 1)
		location.setRoot(values[0]);
	else if (directive == "index" && values.size() == 1)
		location.setIndex(values[0]);
	else if (directive == "allow_methods")
	{
		// Solo aceptamos métodos HTTP conocidos para detectar erratas en la config
		for (size_t i = 0; i < values.size(); ++i)
			if (values[i] != "GET" && values[i] != "POST" && values[i] != "DELETE" &&
				values[i] != "HEAD" && values[i] != "PUT")
				throw std::runtime_error("Unknown method in allow_methods: " + values[i]);
		location.setAllowedMethods(values);
	}
	else if (directive == "autoindex" && values.size() == 1 &&
			 (values[0] == "on" || values[0] == "off"))
		location.setAutoindex(values[0] == "on");
	else if (directive == "return" && (values.size() == 1 || values.size() == 2))
	{
		// "return /url;" (301 por defecto) o "return 302 /url;"
		if (values.size() == 1)
			location.setReturn(301, values[0]);
		else
		{
			int code = parseInteger(values[0], directive);
			if (code < 300 || code > 399)  // solo códigos de redirección
				throw std::runtime_error("Invalid status code in return: " + values[0]);
			location.setReturn(code, values[1]);
		}
	}
	else if (directive == "alias" && values.size() == 1)
		location.setAlias(values[0]);
	else if (directive == "cgi_path")   // uno o varios intérpretes
		location.setCgiPaths(values);
	else if (directive == "cgi_ext")    // una o varias extensiones
		location.setCgiExtensions(values);
	else if (directive == "upload_store" && values.size() == 1)
		location.setUploadStore(values[0]);
	else if (directive == "client_max_body_size" && values.size() == 1)
		location.setClientMaxBodySize(parseUnsignedLong(values[0], directive));
	else
		throw std::runtime_error("Invalid or unknown location directive: " + directive);
}

/* ======================================================================
 *  Fase 2: bloques
 * ====================================================================== */

// "location /ruta { directivas... }"  (la palabra "location" ya se ha leído)
Location ParseConfig::parseLocation()
{
	Location location;
	location.setPath(nextToken());
	// Las URIs siempre empiezan por '/', así que la ruta también
	if (location.getPath().empty() || location.getPath()[0] != '/')
		throw std::runtime_error("Location path must start with '/'");
	expect("{");
	while (hasNext() && _tokens[_position] != "}")
		parseLocationDirective(location, nextToken());
	expect("}");
	return location;
}

// "server { directivas... location ... }"
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
	// Un server que no escucha en ningún puerto no sirve para nada
	if (server.getPorts().empty())
		throw std::runtime_error("Server has no valid listen directive");
	return server;
}

// Punto de entrada: archivo -> lista de Servers validados
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
