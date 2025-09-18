#include "ProtocolHandler.h"
#include "JsonSchemaValidator.h"
#include "Logger.h"
#include <nlohmann/json.hpp>
#include <iostream>

static const nlohmann::json requestSchema = R"({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "properties": {
    "command": {"type": "string"},
    "payload": {"type": "object"}
  },
  "required": ["command"]
})"_json;

static const nlohmann::json responseSchema = R"({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "properties": {
    "status": {"type": "string"}
  },
  "required": ["status"]
})"_json;

ProtocolHandler::ProtocolHandler(){}

nlohmann::json ProtocolHandler::handleRequest(const nlohmann::json& req){
    Logger::getInstance().log("ProtocolHandler handling request");
    nlohmann::json res = { {"status","ok"}, {"payload", req} };
    return res;
}

std::string ProtocolHandler::createResponse(const nlohmann::json& data){
    JsonSchemaValidator v(responseSchema);
    if(!v.validate(data)){
        Logger::getInstance().log(std::string("Response validation failed: ") + v.getErrorMessage());
        return std::string("{\"error\":\"Invalid response\"}");
    }
    return data.dump();
}

bool ProtocolHandler::validateRequest(const nlohmann::json& req){
    JsonSchemaValidator v(requestSchema);
    if(!v.validate(req)){
        Logger::getInstance().log(std::string("Request validation failed: ") + v.getErrorMessage());
        return false;
    }
    return true;
}
