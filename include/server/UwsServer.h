#pragma once
#include "server/IServer.h"
#include <vector>
#include <tuple>
#include <string>

class UwsServer : public IServer {
public:
    UwsServer();
    void addRoute(const std::string& method, const std::string& path,
                  RouteHandler handler) override;
    void listen(const std::string& host, int port) override;
    void stop() override;

private:
    std::vector<std::tuple<std::string, std::string, RouteHandler>> routes_;
    bool running_ = false;
};
