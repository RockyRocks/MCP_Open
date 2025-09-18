#pragma once
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <string>

class JsonSchemaValidator {
public:
    JsonSchemaValidator() = delete;
    JsonSchemaValidator(const nlohmann::json& schema);
    bool validate(const nlohmann::json& data);
    std::string getErrorMessage() const;
private:
    nlohmann::json_schema::json_validator validator;
    std::string lastError;
};
