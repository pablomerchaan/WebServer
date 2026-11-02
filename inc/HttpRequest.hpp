#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include <map>
#include <string>

class HttpRequest
{
public:
    enum State
    {
        HEADERS,
        HEADERS_DONE,
        BODY,
        CHUNK_SIZE,
        CHUNK_DATA,
        CHUNK_TRAILER,
        COMPLETE,
        ERROR
    };

private:
    State _state;
    std::string _buffer;
    std::string _method;
    std::string _target;
    std::string _path;
    std::string _query;
    std::string _version;
    std::map<std::string, std::string> _headers;
    std::string _body;
    size_t _content_length;
    size_t _chunk_remaining;
    unsigned long _max_body_size;
    int _error_code;

    State fail(int code);
    bool reject(int code);
    bool parseHeaderSection(const std::string& section);
    bool parseRequestLine(const std::string& line);
    bool parseTarget();
    void parseBody();
    void parseChunks();

public:
    HttpRequest();

    void append(const char* data, size_t size);

    State parse();

    void setMaxBodySize(unsigned long size);

    void reset();

    State getState() const;
    int getErrorCode() const;
    const std::string& getMethod() const;
    const std::string& getTarget() const;
    const std::string& getPath() const;
    const std::string& getQuery() const;
    const std::string& getVersion() const;
    const std::map<std::string, std::string>& getHeaders() const;
    std::string getHeader(const std::string& name) const;
    const std::string& getBody() const;
    bool hasBufferedData() const;
    bool keepAlive() const;
};

#endif
