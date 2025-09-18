#pragma once
#include <nlohmann/json.hpp>

class IResponseHandler {
public:
    virtual ~IResponseHandler() = default;
    virtual std::string buildResponse(const nlohmann::json& data) = 0;
};
