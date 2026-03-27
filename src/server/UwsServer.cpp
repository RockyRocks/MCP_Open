#include <server/UwsServer.h>
#include <security/SecurityHeaders.h>
#include <core/CompilerDefinitions.h>

#ifdef USE_UWS
#include <App.h>
#endif

#include <iostream>

UwsServer::UwsServer() = default;

void UwsServer::AddRoute(const std::string& method, const std::string& path,
                           RouteHandler handler) {
    m_Routes.emplace_back(method, path, std::move(handler));
}

void UwsServer::Listen(const std::string& host, int port) {
#ifdef USE_UWS
    auto secHeaders = SecurityHeaders::GetDefaults();
    auto app = uWS::App();

    for (auto& [method, path, handler] : m_Routes) {
        if (method == "POST") {
            app.post(path, [handler, secHeaders](auto* res, MAYBE_UNUSED auto* req) {
                res->onData([handler, secHeaders, res](std::string_view data, bool last) {
                    if (!last) return;
                    std::string body(data);
                    std::string clientIp = "0.0.0.0"; // uWS doesn't easily expose this

                    handler(body, clientIp,
                            [res, secHeaders](int status, const std::string& response) {
                                std::string statusStr = std::to_string(status) + " ";
                                if (status == 200) statusStr += "OK";
                                else if (status == 400) statusStr += "Bad Request";
                                else if (status == 401) statusStr += "Unauthorized";
                                else if (status == 413) statusStr += "Payload Too Large";
                                else if (status == 429) statusStr += "Too Many Requests";
                                else statusStr += "Error";

                                res->writeStatus(statusStr);
                                for (const auto& [k, v] : secHeaders) {
                                    res->writeHeader(k, v);
                                }
                                res->writeHeader("Content-Type", "application/json");
                                res->end(response);
                            });
                });
            });
        } else if (method == "GET") {
            app.get(path, [handler, secHeaders](auto* res, MAYBE_UNUSED auto* req) {
                handler("", "0.0.0.0",
                        [res, secHeaders](int status, const std::string& response) {
                            (void)status;
                            for (const auto& [k, v] : secHeaders) {
                                res->writeHeader(k, v);
                            }
                            res->writeHeader("Content-Type", "application/json");
                            res->end(response);
                        });
            });
        }
    }

    m_Running = true;
    app.listen(host, port, [port](auto* token) {
        if (token) std::cout << "UwsServer listening on port " << port << std::endl;
    }).run();
#else
    (void)host;
    (void)port;
    std::cerr << "UwsServer not available (compiled without USE_UWS)" << std::endl;
#endif
}

void UwsServer::Stop() {
    m_Running = false;
}
