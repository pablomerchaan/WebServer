#include "Webserver.hpp"
#include <iostream>

int main(int argc, char **argv)
{
    if (argc > 2)
    {
        std::cerr << "Usage: " << argv[0] << " [configuration file]" << std::endl;
        return (1);
    }
    try
    {
        std::string filename = argc == 2 ? argv[1] : "confs/default.conf";

        ParseConfig parser;
        std::vector<Server> servers = parser.parse(filename);

        ServerManager manager(servers);
        manager.setup();
        manager.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return (1);
    }
    return (0);
}
