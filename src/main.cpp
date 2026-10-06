#include "Webserver.hpp"
#include <iostream>

/*
 * Uso: ./webserv [archivo.conf]
 * Sin argumento se usa confs/default.conf (el subject permite un path por defecto).
 */
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

        // 1) Leer y validar la configuración (lanza excepción si hay errores)
        ParseConfig parser;
        std::vector<Server> servers = parser.parse(filename);

        // 2) Abrir los sockets de escucha y 3) entrar en el bucle de poll()
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
