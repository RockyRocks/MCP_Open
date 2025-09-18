#include <iostream>
#include <httplib.h>
#include <nlohmann/json.hpp>

int main(){
    httplib::Client cli("localhost", 9001);
    nlohmann::json req = { {"command","echo"}, {"payload", { {"msg","hello from client"} }} };
    auto res = cli.Post("/mcp", req.dump(), "application/json");
    if(res && res->status == 200){
        std::cout << "Response: " << res->body << std::endl;
    } else {
        std::cerr << "Request failed" << std::endl;
    }
    return 0;
}
