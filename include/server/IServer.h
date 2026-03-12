#pragma once
#include <functional>
#include <string>

using RouteHandler = std::function<void(
    const std::string& body,
    const std::string& clientIp,
    std::function<void(int status, const std::string& response)> respond)>;

class IServer {
public:
    virtual ~IServer() = default;
    virtual void addRoute(const std::string& method, const std::string& path,
                          RouteHandler handler) = 0;
    virtual void listen(const std::string& host, int port) = 0;
    virtual void stop() = 0;
};
