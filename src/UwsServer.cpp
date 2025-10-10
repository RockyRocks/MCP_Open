#include <App.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include "ProtocolHandler.h"
#include "ThreadPool.h"

void run_uws_server(int port) {
    ProtocolHandler handler;
    ThreadPool pool(4);

    uWS::App().post("/mcp", [&handler,&pool](auto *res, [[maybe_unsed]] auto *req){
        res->onData([&handler,&pool,res](std::string_view data, bool last){
            if(!last) return;
            try{
                auto j = nlohmann::json::parse(data);
                if(!handler.validateRequest(j)){
                    res->writeStatus("400 Bad Request")->end("{\"error\":\"invalid request\"}");
                    return;
                }
                auto fut = pool.submit([j](){
                    nlohmann::json r = { {"status","ok"}, {"echo", j} };
                    return r;
                });
                auto r = fut.get();
                res->writeHeader("Content-Type","application/json")->end(r.dump());
            } catch(const std::exception& e){
                res->writeStatus("500 Internal Server Error")->end(std::string("{\"error\":\"")+e.what()+"\"}");
            }
        });
    }).get("/health", [](auto *res, [[maybe_unsed]] auto* req){
        res->writeHeader("Content-Type","application/json")->end("{\"status\":\"ok\"}");
    }).listen("0.0.0.0", port, [port](auto *token){
        if(token) std::cout << "uWebSockets listening on " << port << std::endl;
    }).run();
}
