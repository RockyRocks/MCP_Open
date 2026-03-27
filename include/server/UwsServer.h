#pragma once
#include <server/IServer.h>
#include <vector>
#include <tuple>
#include <string>

class UwsServer : public IServer {
public:
    UwsServer();
    void AddRoute(const std::string& method, const std::string& path,
                  RouteHandler handler) override;
    void Listen(const std::string& host, int port) override;
    void Stop() override;

private:
    std::vector<std::tuple<std::string, std::string, RouteHandler>> m_Routes;
    bool m_Running = false;
};
