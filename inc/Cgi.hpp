#ifndef CGI_HPP
#define CGI_HPP

#include <ctime>
#include <string>
#include <vector>
#include <sys/types.h>
#include "HttpResponse.hpp"

class Cgi
{
private:
    pid_t _pid;
    int _input_fd;
    int _output_fd;
    std::string _input;
    size_t _input_offset;
    std::string _output;
    std::time_t _start_time;

    Cgi(const Cgi&);
    Cgi& operator=(const Cgi&);

public:
    Cgi();
    ~Cgi();

    bool start(const std::string& interpreter, const std::string& script,
               const std::vector<std::string>& env, const std::string& body);

    int getInputFd() const;
    int getOutputFd() const;
    std::time_t getStartTime() const;

    void writeInput();

    bool readOutput();
    void closeInput();

    void stop();

    HttpResponse buildResponse() const;
};

#endif
