#include "../inc/Cgi.hpp"
#include "../inc/Utils.hpp"
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

Cgi::Cgi() : _pid(-1), _input_fd(-1), _output_fd(-1), _input_offset(0), _start_time(0) {}

Cgi::~Cgi()
{
	stop();
}

// Cierra los dos extremos de una pipe (los que estén abiertos)
static void closePipe(int fds[2])
{
	if (fds[0] != -1)
		close(fds[0]);
	if (fds[1] != -1)
		close(fds[1]);
}

/*
 * Prepara el extremo de la pipe que se queda el servidor:
 *  - O_NONBLOCK: read/write nunca bloquean (el subject lo exige)
 *  - FD_CLOEXEC: se cierra solo en cualquier execve(). MUY importante: si
 *    lanzamos dos CGIs a la vez, el segundo heredaría el extremo de escritura
 *    del stdin del primero; entonces el primero nunca recibiría EOF en su
 *    stdin (alguien seguiría teniendo la pipe abierta) y se quedaría colgado.
 */
static bool configureParentEnd(int fd)
{
	return fcntl(fd, F_SETFL, O_NONBLOCK) != -1 && fcntl(fd, F_SETFD, FD_CLOEXEC) != -1;
}

bool Cgi::start(const std::string& interpreter, const std::string& script,
				const std::vector<std::string>& env, const std::string& body)
{
	// pipe(fds): lo que se escribe en fds[1] se lee en fds[0]
	int to_child[2] = {-1, -1};    // servidor escribe [1] -> script lee [0] (stdin)
	int from_child[2] = {-1, -1};  // script escribe [1] (stdout) -> servidor lee [0]
	if (pipe(to_child) == -1 || pipe(from_child) == -1)
	{
		closePipe(to_child);
		closePipe(from_child);
		return false;
	}
	if (!configureParentEnd(to_child[1]) || !configureParentEnd(from_child[0]))
	{
		closePipe(to_child);
		closePipe(from_child);
		return false;
	}

	/*
	 * El subject pide ejecutar el CGI "en el directorio correcto" para que
	 * pueda abrir ficheros con rutas relativas. Separamos la ruta:
	 *   "www/cgi-bin/time.py" -> directorio "www/cgi-bin", fichero "time.py"
	 * El hijo hará chdir() al directorio y ejecutará "./time.py".
	 */
	std::string directory = ".";
	std::string filename = script;
	size_t slash = script.rfind('/');
	if (slash != std::string::npos)
	{
		directory = slash == 0 ? "/" : script.substr(0, slash);
		filename = script.substr(slash + 1);
	}
	std::string relative_script = "./" + filename;

	/*
	 * Preparamos argv y envp ANTES del fork. execve necesita arrays de char*
	 * terminados en NULL:
	 *   argv = { "/usr/bin/python3", "./time.py", NULL }   (con intérprete)
	 *   argv = { "./time.py", NULL }                       (sin intérprete)
	 *   envp = { "REQUEST_METHOD=GET", "QUERY_STRING=...", ..., NULL }
	 */
	std::vector<char*> envp;
	for (size_t i = 0; i < env.size(); ++i)
		envp.push_back(const_cast<char*>(env[i].c_str()));
	envp.push_back(NULL);
	std::vector<char*> argv;
	if (!interpreter.empty())
		argv.push_back(const_cast<char*>(interpreter.c_str()));
	argv.push_back(const_cast<char*>(relative_script.c_str()));
	argv.push_back(NULL);

	_pid = fork();
	if (_pid == -1)
	{
		closePipe(to_child);
		closePipe(from_child);
		return false;
	}
	if (_pid == 0)
	{
		/* ---------------- PROCESO HIJO ---------------- */
		// Conectamos las pipes a la entrada/salida estándar del script
		if (dup2(to_child[0], STDIN_FILENO) == -1 || dup2(from_child[1], STDOUT_FILENO) == -1)
			std::exit(1);
		// Los originales ya no hacen falta (stdin/stdout son copias)
		closePipe(to_child);
		closePipe(from_child);
		if (chdir(directory.c_str()) == -1)
			std::exit(1);
		// Si execve funciona, este proceso pasa a ser el script y no vuelve
		execve(argv[0], &argv[0], &envp[0]);
		// Si llegamos aquí execve falló: el padre verá una salida vacía -> 502
		std::exit(1);
	}

	/* ---------------- PROCESO PADRE (servidor) ---------------- */
	// Cerramos los extremos que usa el hijo. Esto es imprescindible: si el
	// padre mantuviera abierto from_child[1], nunca veríamos EOF al leer.
	close(to_child[0]);
	close(from_child[1]);
	_input_fd = to_child[1];
	_output_fd = from_child[0];
	_input = body;
	_input_offset = 0;
	_start_time = std::time(NULL);
	// Sin body (p.ej. GET), cerramos ya su stdin para que el script vea EOF
	if (_input.empty())
		closeInput();
	return true;
}

int Cgi::getInputFd() const { return _input_fd; }
int Cgi::getOutputFd() const { return _output_fd; }
std::time_t Cgi::getStartTime() const { return _start_time; }

void Cgi::closeInput()
{
	if (_input_fd != -1)
		close(_input_fd);
	_input_fd = -1;
}

/*
 * Llamado cuando poll() dice que la pipe admite escritura. Un único write():
 * puede que no quepa todo el body (la pipe tiene un buffer de ~64 KB), así
 * que recordamos por dónde vamos y seguimos en el siguiente evento.
 */
void Cgi::writeInput()
{
	if (_input_fd == -1)
		return;
	ssize_t written = write(_input_fd, _input.data() + _input_offset,
							_input.size() - _input_offset);
	// Error: el script dejó de leer (o ya terminó). No miramos errno (el
	// subject lo prohíbe tras read/write); simplemente dejamos de enviarle.
	if (written <= 0)
	{
		closeInput();
		return;
	}
	_input_offset += written;
	// Body enviado entero: cerrar la pipe = EOF en el stdin del script
	if (_input_offset >= _input.size())
		closeInput();
}

/*
 * Llamado cuando poll() dice que hay datos para leer (o que el script cerró
 * su stdout, POLLHUP). Un único read(): 0 bytes = EOF = el script terminó.
 */
bool Cgi::readOutput()
{
	char buffer[65536];
	ssize_t bytes = read(_output_fd, buffer, sizeof(buffer));
	if (bytes <= 0)
	{
		close(_output_fd);
		_output_fd = -1;
		return false;
	}
	_output.append(buffer, bytes);
	return true;
}

void Cgi::stop()
{
	closeInput();
	if (_output_fd != -1)
		close(_output_fd);
	_output_fd = -1;
	if (_pid > 0)
	{
		/*
		 * Hay que "recoger" (waitpid) todo hijo que termina; si no, queda como
		 * proceso zombi. WNOHANG = no esperar: si ya terminó lo recoge y
		 * devuelve su pid; si sigue vivo devuelve 0. En ese caso (timeout o
		 * cliente desconectado) lo matamos con SIGKILL y ahora sí esperamos,
		 * lo que es instantáneo porque SIGKILL no se puede ignorar.
		 */
		int status;
		if (waitpid(_pid, &status, WNOHANG) == 0)
		{
			kill(_pid, SIGKILL);
			waitpid(_pid, &status, 0);
		}
	}
	_pid = -1;
}

/*
 * Un script CGI escribe sus propias cabeceras, una línea vacía y el cuerpo:
 *
 *     Status: 404 Not Found         <- opcional (por defecto 200)
 *     Content-Type: text/html       <- tipo del contenido
 *     Location: /otra               <- opcional: redirección
 *
 *     <h1>...</h1>
 *
 * Lo transformamos en una HttpResponse normal.
 */
HttpResponse Cgi::buildResponse() const
{
	HttpResponse response;
	size_t end = _output.find("\r\n\r\n");
	size_t separator = 4;
	if (end == std::string::npos)
	{
		// Muchos scripts usan print() y solo generan '\n'
		end = _output.find("\n\n");
		separator = 2;
	}
	if (end == std::string::npos)
	{
		// Sin separación cabeceras/cuerpo (o sin salida: script roto o execve
		// fallido): 502 Bad Gateway = "el programa que hay detrás falló"
		response.setStatus(502);
		response.setBody(defaultErrorPage(502), "text/html");
		return response;
	}

	std::string headers = _output.substr(0, end);
	response.setBody(_output.substr(end + separator));
	bool has_content_type = false;
	// Recorremos las cabeceras del script línea a línea
	size_t start = 0;
	while (start < headers.size())
	{
		size_t line_end = headers.find('\n', start);
		if (line_end == std::string::npos)
			line_end = headers.size();
		std::string line = trim(headers.substr(start, line_end - start));
		start = line_end + 1;
		size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		std::string name = trim(line.substr(0, colon));
		std::string value = trim(line.substr(colon + 1));
		std::string lower = toLower(name);
		if (lower == "status")
		{
			// "Status: 404 Not Found" -> 404 (atoi se para en el espacio)
			int code = std::atoi(value.c_str());
			response.setStatus(code >= 100 && code <= 599 ? code : 500);
		}
		else if (lower == "content-length" || lower == "connection")
			continue; // las calcula HttpResponse::build(), no nos fiamos del script
		else
		{
			if (lower == "content-type")
				has_content_type = true;
			else if (lower == "location" && response.getStatus() == 200)
				response.setStatus(302);  // Location sin Status = redirección
			response.setHeader(name, value);
		}
	}
	if (!has_content_type)
		response.setHeader("Content-Type", "text/html");
	return response;
}
