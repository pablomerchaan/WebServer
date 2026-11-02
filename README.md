*This project has been created as part of the 42 curriculum by jaigonza, almarti3, paperez-*

# Description

Webserv is a C++98 HTTP server developed for the 42 curriculum. It implements a
non-blocking server based on `poll()` and supports multiple virtual servers,
static files, directory listings, HTTP redirections, file uploads, configurable
error pages, and CGI execution.

The server reads one or more server definitions from an nginx-inspired
configuration file. Each server can define its listening address and port,
document root, index file, allowed methods, body-size limits, locations, and
CGI interpreters.

# Instructions

## Requirements

- A C++ compiler with C++98 support.
- `make`.
- Python 3 and Bash if the CGI examples are used.

## Compilation

From the repository root, build the executable with:

```sh
make
```

The executable is created as `./webserv`. To remove object files, run
`make clean`; to remove all generated build files, run `make fclean`.

## Execution

Start the server with the default configuration:

```sh
./webserv
```

The default configuration listens on `127.0.0.1:8002` and also defines a
second virtual server on port `8003`. A custom configuration file can be
provided as an argument:

```sh
./webserv path/to/config.conf
```

Once the server is running, useful examples include:

```sh
curl http://127.0.0.1:8002/
curl -X POST -d "name=42" http://127.0.0.1:8002/uploads
curl -X DELETE http://127.0.0.1:8002/uploads/file.txt
curl http://127.0.0.1:8002/cgi-bin/time.py
```

The configuration syntax and example routes are documented in
[`confs/default.conf`](./confs/default.conf). The `www/` directory contains
the files served by the default configuration.

# Resources

- [HTTP Semantics (RFC 9110)](https://www.rfc-editor.org/rfc/rfc9110)
  for HTTP methods, status codes, headers, and message semantics.
- [The `poll()` Linux manual page](https://man7.org/linux/man-pages/man2/poll.2.html)
  for the event-driven I/O model used by the server.
- [CGI 1.1 specification](https://www.rfc-editor.org/rfc/rfc3875)
  for the interface between the server and CGI scripts.
- [C++ reference](https://en.cppreference.com/w/cpp) for C++98 language and
  standard-library details.

AI was used to learn and understand concepts covered in the project.
