#include <validation/JsonSchemaValidator.h>

JsonSchemaValidator::JsonSchemaValidator(const nlohmann::json& schema) {
    m_Validator.set_root_schema(schema);
}

bool JsonSchemaValidator::Validate(const nlohmann::json& data) {
    try {
        m_Validator.validate(data);
        return true;
    } catch (const std::exception& e) {
        m_LastError = e.what();
        return false;
    }
}

std::string JsonSchemaValidator::GetErrorMessage() const { return m_LastError; }
