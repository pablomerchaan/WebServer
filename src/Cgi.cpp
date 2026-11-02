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

static void closePipe(int fds[2])
{
	if (fds[0] != -1)
		close(fds[0]);
	if (fds[1] != -1)
		close(fds[1]);
}

static bool configureParentEnd(int fd)
{
	return fcntl(fd, F_SETFL, O_NONBLOCK) != -1 && fcntl(fd, F_SETFD, FD_CLOEXEC) != -1;
}

bool Cgi::start(const std::string& interpreter, const std::string& script,
				const std::vector<std::string>& env, const std::string& body)
{

	int to_child[2] = {-1, -1};
	int from_child[2] = {-1, -1};
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

	std::string directory = ".";
	std::string filename = script;
	size_t slash = script.rfind('/');
	if (slash != std::string::npos)
	{
		directory = slash == 0 ? "/" : script.substr(0, slash);
		filename = script.substr(slash + 1);
	}
	std::string relative_script = "./" + filename;

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

		if (dup2(to_child[0], STDIN_FILENO) == -1 || dup2(from_child[1], STDOUT_FILENO) == -1)
			std::exit(1);

		closePipe(to_child);
		closePipe(from_child);
		if (chdir(directory.c_str()) == -1)
			std::exit(1);

		execve(argv[0], &argv[0], &envp[0]);

		std::exit(1);
	}

	close(to_child[0]);
	close(from_child[1]);
	_input_fd = to_child[1];
	_output_fd = from_child[0];
	_input = body;
	_input_offset = 0;
	_start_time = std::time(NULL);

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

void Cgi::writeInput()
{
	if (_input_fd == -1)
		return;
	ssize_t written = write(_input_fd, _input.data() + _input_offset,
							_input.size() - _input_offset);

	if (written <= 0)
	{
		closeInput();
		return;
	}
	_input_offset += written;

	if (_input_offset >= _input.size())
		closeInput();
}

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

		int status;
		if (waitpid(_pid, &status, WNOHANG) == 0)
		{
			kill(_pid, SIGKILL);
			waitpid(_pid, &status, 0);
		}
	}
	_pid = -1;
}

HttpResponse Cgi::buildResponse() const
{
	HttpResponse response;
	size_t end = _output.find("\r\n\r\n");
	size_t separator = 4;
	if (end == std::string::npos)
	{

		end = _output.find("\n\n");
		separator = 2;
	}
	if (end == std::string::npos)
	{

		response.setStatus(502);
		response.setBody(defaultErrorPage(502), "text/html");
		return response;
	}

	std::string headers = _output.substr(0, end);
	response.setBody(_output.substr(end + separator));
	bool has_content_type = false;

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

			int code = std::atoi(value.c_str());
			response.setStatus(code >= 100 && code <= 599 ? code : 500);
		}
		else if (lower == "content-length" || lower == "connection")
			continue;
		else
		{
			if (lower == "content-type")
				has_content_type = true;
			else if (lower == "location" && response.getStatus() == 200)
				response.setStatus(302);
			response.setHeader(name, value);
		}
	}
	if (!has_content_type)
		response.setHeader("Content-Type", "text/html");
	return response;
}
