#include "JsonSchemaValidator.h"

JsonSchemaValidator::JsonSchemaValidator(const nlohmann::json& schema) {
    validator.set_root_schema(schema);
    validator.set_error_handler([this](const nlohmann::json::json_pointer& ptr, const nlohmann::json& instance){
        lastError = "Invalid field at: " + ptr.to_string();
    });
}

bool JsonSchemaValidator::validate(const nlohmann::json& data) {
    try {
        validator.validate(data);
        return true;
    } catch(const std::exception& e) {
        lastError = e.what();
        return false;
    }
}

std::string JsonSchemaValidator::getErrorMessage() const { return lastError; }
