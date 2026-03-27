#pragma once
#include <server/IServer.h>
#include <httplib.h>
#include <memory>
#include <vector>
#include <tuple>

class HttplibServer : public IServer {
public:
    HttplibServer();
    void AddRoute(const std::string& method, const std::string& path,
                  RouteHandler handler) override;
    void Listen(const std::string& host, int port) override;
    void Stop() override;

private:
    httplib::Server m_Server;
    // Deferred route registration (applied in Listen())
    std::vector<std::tuple<std::string, std::string, RouteHandler>> m_Routes;
};
