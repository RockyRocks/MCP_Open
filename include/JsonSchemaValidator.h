#pragma once
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <string>

class JsonSchemaValidator {
public:
    JsonSchemaValidator() = delete;
    explicit JsonSchemaValidator(const nlohmann::json& schema);
    bool validate(const nlohmann::json& data/*, const nlohmann::json_schema::basic_error_handler& error*/);
    std::string getErrorMessage() const;
private:
    nlohmann::json_schema::json_validator validator;
    // nlohmann::json_schema::basic_error_handler errorHandler;
    std::string lastError;
};
