#include <iostream>
#if USE_UWS
// Test using uWS
#else
#include <httplib.h>
#endif
#include <nlohmann/json.hpp>

int main(){
#if USE_UWS
    // Client using uWebSockets
#else
    httplib::Client cli("localhost", 9001);
    nlohmann::json req = { {"command","echo"}, {"payload", { {"msg","hello from client"} }} };
    auto res = cli.Post("/mcp", req.dump(), "application/json");
    if (res && res->status == 200) {
        std::cout << "Response: " << res->body << std::endl;
    }
    else {
        std::cerr << "Request failed" << std::endl;
    }
#endif
    return 0;
}
