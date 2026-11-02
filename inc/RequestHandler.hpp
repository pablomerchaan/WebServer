#ifndef REQUEST_HANDLER_HPP
#define REQUEST_HANDLER_HPP

#include <string>
#include <vector>
#include "Cgi.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "Server.hpp"

class RequestHandler
{
private:
    const Server& _server;
    const HttpRequest& _request;
    std::string _client_address;
    int _port;
    Location _fallback;
    const Location* _location;

    std::string mapPath(const std::string& uri_path) const;

    std::string indexName() const;

    bool findCgiScript(const std::string& uri_path, std::string& script_uri,
                       std::string& path_info) const;

    HttpResponse handleGet(const std::string& path);
    HttpResponse handlePost(const std::string& path);
    HttpResponse handleDelete(const std::string& path);
    HttpResponse serveFile(const std::string& path);
    HttpResponse autoindex(const std::string& path);
    HttpResponse redirect(int code, const std::string& location);
    HttpResponse saveUploads(const std::string& directory);
    bool startCgi(const std::string& script_uri, const std::string& path_info, Cgi*& cgi);

public:
    RequestHandler(const Server& server, const HttpRequest& request,
                   const std::string& client_address, int port);

    HttpResponse handle(Cgi*& cgi);

    static const Server& selectServer(const std::vector<const Server*>& servers,
                                      const HttpRequest& request);

    static unsigned long maxBodySize(const Server& server, const HttpRequest& request);

    static HttpResponse errorResponse(const Server& server, int code);
};

#endif
