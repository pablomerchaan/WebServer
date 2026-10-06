#ifndef PARSE_CONFIG_HPP
#define PARSE_CONFIG_HPP

#include <string>
#include <vector>
#include "Server.hpp"

/*
 * ParseConfig: lee el archivo de configuración (sintaxis tipo nginx) y lo
 * convierte en una lista de objetos Server, cada uno con sus Location.
 * Si el archivo tiene cualquier error lanza std::runtime_error.
 *
 * Uso:
 *     ParseConfig parser;
 *     std::vector<Server> servers = parser.parse("confs/default.conf");
 */
class ParseConfig
{
private:
    std::vector<std::string> _tokens;  // el archivo troceado en palabras
    size_t _position;                  // índice del próximo token a leer

    // Lectura y troceado del archivo
    std::string readFile(const std::string& filename) const;
    void tokenize(const std::string& content);

    // Navegación por los tokens
    void expect(const std::string& token);
    std::string nextToken();
    bool hasNext() const;

    // Gramática: bloques y directivas
    Server parseServer();
    Location parseLocation();
    void parseServerDirective(Server& server, const std::string& directive);
    void parseLocationDirective(Location& location, const std::string& directive);

    // Conversión/validación de valores
    int parseInteger(const std::string& value, const std::string& directive) const;
    unsigned long parseUnsignedLong(const std::string& value,
                                    const std::string& directive) const;
    int parsePort(const std::string& value) const;
    std::vector<std::string> readValuesUntilSemicolon();

public:
    ParseConfig();
    std::vector<Server> parse(const std::string& filename);
};

#endif
