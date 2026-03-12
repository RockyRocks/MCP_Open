#include "server/HttplibServer.h"
#include "security/SecurityHeaders.h"
#include <iostream>

HttplibServer::HttplibServer() = default;

void HttplibServer::addRoute(const std::string& method, const std::string& path,
                               RouteHandler handler) {
    routes_.emplace_back(method, path, std::move(handler));
}

void HttplibServer::listen(const std::string& host, int port) {
    auto secHeaders = SecurityHeaders::getDefaults();

    for (auto& [method, path, handler] : routes_) {
        auto routeHandler = [handler, secHeaders](const httplib::Request& req,
                                                    httplib::Response& res) {
            // Apply security headers
            for (const auto& [k, v] : secHeaders) {
                res.set_header(k, v);
            }
            res.set_header("Content-Type", "application/json");

            std::string clientIp = req.remote_addr;
            handler(req.body, clientIp, [&res](int status, const std::string& response) {
                res.status = status;
                res.set_content(response, "application/json");
            });
        };

        if (method == "POST") {
            svr_.Post(path, routeHandler);
        } else if (method == "GET") {
            svr_.Get(path, routeHandler);
        }
    }

    std::cout << "HttplibServer listening on " << host << ":" << port << std::endl;
    svr_.listen(host, port);
}

void HttplibServer::stop() {
    svr_.stop();
}
