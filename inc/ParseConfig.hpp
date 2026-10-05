#ifndef PARSE_CONFIG_HPP
#define PARSE_CONFIG_HPP

#include <string>
#include <vector>
#include "Server.hpp"

class ParseConfig
{
private:
    std::vector<std::string> _tokens;
    size_t _position;

    std::string readFile(const std::string& filename) const;
    void tokenize(const std::string& content);
    void expect(const std::string& token);
    std::string nextToken();
    bool hasNext() const;
    Server parseServer();
    Location parseLocation();
    void parseServerDirective(Server& server, const std::string& directive);
    void parseLocationDirective(Location& location, const std::string& directive);
    int parseInteger(const std::string& value, const std::string& directive) const;
    unsigned long parseUnsignedLong(const std::string& value,
                                    const std::string& directive) const;
    std::vector<std::string> readValuesUntilSemicolon();

public:
    ParseConfig();
    std::vector<Server> parse(const std::string& filename);
};

#endif
