#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include "ProtocolHandler.h"
#include "ThreadPool.h"

void run_httplib_server(int port) {
    httplib::Server svr;
    ProtocolHandler handler;
    ThreadPool pool(4);

    svr.Post("/mcp", [&](const httplib::Request& req, httplib::Response& res){
        try {
            auto j = nlohmann::json::parse(req.body);
            if(!handler.validateRequest(j)){
                res.status = 400;
                res.set_content("{\"error\":\"invalid request\"}", "application/json");
                return;
            }
            auto fut = pool.submit([j](){
                nlohmann::json r = { {"status","ok"}, {"echo", j} };
                return r;
            });
            auto r = fut.get();
            res.set_content(r.dump(), "application/json");
        } catch(const std::exception& e){
            res.status = 500;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    svr.Get("/health", [&](const httplib::Request&, httplib::Response& r){
        r.set_content("{\"status\":\"ok\"}", "application/json");
    });

    std::cout << "Starting httplib server on port " << port << std::endl;
    svr.listen("0.0.0.0", port);
}
