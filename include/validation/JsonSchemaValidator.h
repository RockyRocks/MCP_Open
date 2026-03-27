#pragma once
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <string>

class JsonSchemaValidator {
public:
    JsonSchemaValidator() = delete;
    explicit JsonSchemaValidator(const nlohmann::json& schema);
    bool Validate(const nlohmann::json& data);
    std::string GetErrorMessage() const;
private:
    nlohmann::json_schema::json_validator m_Validator;
    std::string m_LastError;
};
