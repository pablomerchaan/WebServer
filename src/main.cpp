#include "Webserver.hpp"
#include <iostream>

int main(int argc, char **argv)
{
    if (argc != 1 && argc != 2)
        return (1);
    try
    {
        std::string filename = argc == 2 ? argv[1] : "confs/default.conf";
        ParseConfig parser;
        std::vector<Server> servers = parser.parse(filename);

        for (size_t i = 0; i < servers.size(); ++i)
        {
            std::cout << "Server " << i + 1 << std::endl;
            std::cout << "  host: " << servers[i].getHost() << std::endl;
            std::cout << "  port: " << servers[i].getPort() << std::endl;
            std::cout << "  root: " << servers[i].getRoot() << std::endl;
            std::cout << "  locations: " << servers[i].getLocations().size() << std::endl;
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return (1);
    }
    return (0);
}