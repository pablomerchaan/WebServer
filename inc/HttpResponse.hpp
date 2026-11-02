#ifndef HTTP_RESPONSE_HPP
#define HTTP_RESPONSE_HPP

#include <map>
#include <string>

class HttpResponse
{
private:
    int _status;
    std::map<std::string, std::string> _headers;
    std::string _body;

public:
    HttpResponse();

    void setStatus(int status);
    void setHeader(const std::string& name, const std::string& value);

    void setBody(const std::string& body, const std::string& content_type);
    void setBody(const std::string& body);

    int getStatus() const;
    bool hasHeader(const std::string& name) const;

    std::string build(bool keep_alive, bool include_body) const;
};

#endif
