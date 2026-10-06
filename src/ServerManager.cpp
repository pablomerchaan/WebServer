#include "../inc/ServerManager.hpp"
#include "../inc/Utils.hpp"
#include <csignal>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <set>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

/*
 * Bandera global que pone a 0 el manejador de señales. Debe ser
 * "volatile sig_atomic_t": es el único tipo que el estándar garantiza que se
 * puede escribir de forma segura desde un manejador de señales.
 */
static volatile sig_atomic_t g_running = 1;

static void handleSignal(int)
{
	g_running = 0;
}

/*
 * Pone un fd en modo no bloqueante (O_NONBLOCK) y hace que se cierre solo al
 * hacer execve (FD_CLOEXEC). Lo segundo evita que los procesos CGI hereden
 * los sockets del servidor.
 */
static bool makeNonBlocking(int fd)
{
	return fcntl(fd, F_SETFL, O_NONBLOCK) != -1 && fcntl(fd, F_SETFD, FD_CLOEXEC) != -1;
}

ServerManager::ServerManager(const std::vector<Server>& servers) : _servers(servers) {}

ServerManager::~ServerManager()
{
	// Borrar un Client cierra su socket y mata/recoge su CGI si lo tuviera
	for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it)
		delete it->second;
	for (size_t i = 0; i < _listeners.size(); ++i)
		if (_listeners[i].fd != -1)
			close(_listeners[i].fd);
}

/* ======================================================================
 *  Arranque: crear los sockets de escucha
 * ====================================================================== */

void ServerManager::setup()
{
	// 1) Agrupar los servers por host:puerto. Si dos bloques "server" escuchan
	//    en el mismo sitio, comparten socket (solo se puede hacer bind una vez).
	for (size_t i = 0; i < _servers.size(); ++i)
	{
		const std::vector<int>& ports = _servers[i].getPorts();
		for (size_t p = 0; p < ports.size(); ++p)
		{
			bool found = false;
			for (size_t l = 0; l < _listeners.size(); ++l)
			{
				if (_listeners[l].host == _servers[i].getHost() && _listeners[l].port == ports[p])
				{
					_listeners[l].servers.push_back(&_servers[i]);
					found = true;
					break;
				}
			}
			if (!found)
			{
				Listener listener;
				listener.fd = -1;
				listener.host = _servers[i].getHost();
				listener.port = ports[p];
				listener.servers.push_back(&_servers[i]);
				_listeners.push_back(listener);
			}
		}
	}
	// 2) Abrir un socket por cada host:puerto distinto
	for (size_t l = 0; l < _listeners.size(); ++l)
	{
		openListener(_listeners[l]);
		std::cout << "Listening on http://" << _listeners[l].host << ":"
				  << _listeners[l].port << std::endl;
	}
}

void ServerManager::openListener(Listener& listener)
{
	std::string where = listener.host + ":" + toString(listener.port);

	// getaddrinfo traduce el host ("localhost", "127.0.0.1", "0.0.0.0"...)
	// a una dirección binaria lista para bind(). AI_PASSIVE = para escuchar.
	struct addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	struct addrinfo* result = NULL;
	int status = getaddrinfo(listener.host.c_str(), toString(listener.port).c_str(), &hints, &result);
	if (status != 0)
		throw std::runtime_error("getaddrinfo(" + where + "): " + gai_strerror(status));

	int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (fd == -1)
	{
		freeaddrinfo(result);
		throw std::runtime_error("socket(" + where + ") failed");
	}
	// SO_REUSEADDR: permite reiniciar el servidor sin esperar a que el
	// sistema libere el puerto (estado TIME_WAIT de conexiones anteriores)
	int enable = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1 ||
		bind(fd, result->ai_addr, result->ai_addrlen) == -1 ||
		listen(fd, SOMAXCONN) == -1 || !makeNonBlocking(fd))
	{
		freeaddrinfo(result);
		close(fd);
		throw std::runtime_error("Cannot listen on " + where + ": " + std::strerror(errno));
	}
	freeaddrinfo(result);
	listener.fd = fd;
}

/* ======================================================================
 *  Bucle principal
 * ====================================================================== */

void ServerManager::run()
{
	// Ctrl+C / kill: salir del bucle limpiamente (cerrando fds y matando CGIs)
	std::signal(SIGINT, handleSignal);
	std::signal(SIGTERM, handleSignal);
	// Si el cliente cierra mientras le escribimos, send() generaría SIGPIPE y
	// mataría el servidor. Lo ignoramos: send() simplemente devolverá -1.
	std::signal(SIGPIPE, SIG_IGN);

	while (g_running)
	{
		buildPollList();
		// Timeout de 1000 ms: aunque no haya actividad, volvemos a revisar
		// timeouts de clientes y CGIs al menos una vez por segundo.
		int ready = poll(&_pollfds[0], _pollfds.size(), 1000);
		if (ready < 0)
			continue; // normalmente EINTR por una señal: g_running decide si seguimos

		// Clientes que hay que cerrar en esta vuelta. Se borran al final para
		// que ninguna otra entrada de esta misma vuelta apunte a memoria liberada.
		std::set<Client*> to_close;

		for (size_t i = 0; i < _pollfds.size() && ready > 0; ++i)
		{
			short revents = _pollfds[i].revents;
			if (revents == 0)
				continue;
			--ready;
			Entry& entry = _entries[i];
			if (entry.type != LISTENER && to_close.count(entry.client))
				continue;

			if (entry.type == LISTENER)
			{
				if (revents & POLLIN)
					acceptClient(_listeners[entry.listener]);
			}
			else if (entry.type == CLIENT)
			{
				bool keep = true;
				if (revents & (POLLERR | POLLNVAL))
					keep = false;
				else if (revents & POLLIN)
					keep = entry.client->onReadable();     // un único recv()
				else if (revents & POLLOUT)
					keep = entry.client->onWritable();     // un único send()
				else if (revents & POLLHUP)
					keep = false;
				if (!keep)
					to_close.insert(entry.client);
			}
			else
			{
				// Pipe de un CGI. Comprobamos que el fd sigue siendo el del CGI
				// actual: pudo cerrarse antes en esta misma vuelta.
				Cgi* cgi = entry.client->getCgi();
				if (cgi == NULL)
					continue;
				if (entry.type == CGI_INPUT && cgi->getInputFd() == _pollfds[i].fd)
				{
					if (revents & POLLOUT)
						entry.client->onCgiWritable();     // un único write()
					else
						cgi->closeInput();                  // el script cerró su stdin
				}
				else if (entry.type == CGI_OUTPUT && cgi->getOutputFd() == _pollfds[i].fd)
					entry.client->onCgiReadable();         // un único read() (0 = EOF)
			}
		}
		for (std::set<Client*>::iterator it = to_close.begin(); it != to_close.end(); ++it)
			closeClient(*it);
		checkTimeouts();
	}
	std::cout << "\nShutting down webserv" << std::endl;
}

void ServerManager::addPollEntry(int fd, short events, EntryType type, size_t listener,
								 Client* client)
{
	pollfd pfd;
	pfd.fd = fd;
	pfd.events = events;
	pfd.revents = 0;
	_pollfds.push_back(pfd);
	Entry entry;
	entry.type = type;
	entry.listener = listener;
	entry.client = client;
	_entries.push_back(entry);
}

/*
 * Reconstruye desde cero la lista de fds a vigilar. Es más simple que ir
 * añadiendo/quitando entradas y con pocos cientos de conexiones el coste es
 * despreciable. Cada fd solo pide el evento que su dueño necesita AHORA:
 *  - cliente leyendo   -> POLLIN
 *  - cliente escribiendo -> POLLOUT
 *  - cliente esperando a su CGI -> nada en el socket; se vigilan las pipes
 */
void ServerManager::buildPollList()
{
	_pollfds.clear();
	_entries.clear();
	for (size_t l = 0; l < _listeners.size(); ++l)
		addPollEntry(_listeners[l].fd, POLLIN, LISTENER, l, NULL);
	for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it)
	{
		Client* client = it->second;
		if (client->getState() == Client::READING)
			addPollEntry(client->getFd(), POLLIN, CLIENT, 0, client);
		else if (client->getState() == Client::WRITING)
			addPollEntry(client->getFd(), POLLOUT, CLIENT, 0, client);
		else if (client->getCgi() != NULL)
		{
			if (client->getCgi()->getInputFd() != -1)
				addPollEntry(client->getCgi()->getInputFd(), POLLOUT, CGI_INPUT, 0, client);
			if (client->getCgi()->getOutputFd() != -1)
				addPollEntry(client->getCgi()->getOutputFd(), POLLIN, CGI_OUTPUT, 0, client);
		}
	}
}

/* ======================================================================
 *  Gestión de clientes
 * ====================================================================== */

void ServerManager::acceptClient(const Listener& listener)
{
	struct sockaddr_in address;
	socklen_t length = sizeof(address);
	int fd = accept(listener.fd, reinterpret_cast<struct sockaddr*>(&address), &length);
	if (fd == -1)
		return;
	if (!makeNonBlocking(fd))
	{
		close(fd);
		return;
	}
	// inet_ntoa no está permitida: formateamos la IP a mano (a.b.c.d)
	unsigned long ip = ntohl(address.sin_addr.s_addr);
	std::string ip_text = toString((ip >> 24) & 0xFF) + "." + toString((ip >> 16) & 0xFF) + "." +
						  toString((ip >> 8) & 0xFF) + "." + toString(ip & 0xFF);
	_clients[fd] = new Client(fd, ip_text, listener.port, listener.servers);
}

void ServerManager::closeClient(Client* client)
{
	_clients.erase(client->getFd());
	delete client; // cierra el socket y para el CGI si sigue vivo
}

void ServerManager::checkTimeouts()
{
	std::time_t now = std::time(NULL);
	std::vector<Client*> expired;
	for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it)
		if (!it->second->checkTimeout(now))
			expired.push_back(it->second);
	for (size_t i = 0; i < expired.size(); ++i)
		closeClient(expired[i]);
}
