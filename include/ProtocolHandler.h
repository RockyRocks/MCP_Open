#pragma once
#include <nlohmann/json.hpp>

class ProtocolHandler {
public:
    ProtocolHandler();
    // returns JSON response
    nlohmann::json handleRequest(const nlohmann::json& req);
    // returns serialized response or error JSON string
    std::string createResponse(const nlohmann::json& data);
    bool validateRequest(const nlohmann::json& req);
};
