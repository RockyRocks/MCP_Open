#pragma once
#include "server/IServer.h"
#include <httplib.h>
#include <memory>
#include <vector>
#include <tuple>

class HttplibServer : public IServer {
public:
    HttplibServer();
    void addRoute(const std::string& method, const std::string& path,
                  RouteHandler handler) override;
    void listen(const std::string& host, int port) override;
    void stop() override;

private:
    httplib::Server svr_;
    // Deferred route registration (applied in listen())
    std::vector<std::tuple<std::string, std::string, RouteHandler>> routes_;
};
