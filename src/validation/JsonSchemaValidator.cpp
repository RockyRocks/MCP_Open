#include "validation/JsonSchemaValidator.h"

JsonSchemaValidator::JsonSchemaValidator(const nlohmann::json& schema) {
    validator.set_root_schema(schema);
}

bool JsonSchemaValidator::validate(const nlohmann::json& data) {
    try {
        validator.validate(data);
        return true;
    } catch (const std::exception& e) {
        lastError = e.what();
        return false;
    }
}

std::string JsonSchemaValidator::getErrorMessage() const { return lastError; }
