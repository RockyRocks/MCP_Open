#include <commands/CommandRegistry.h>
#include <stdexcept>

void CommandRegistry::RegisterCommand(const std::string& name,
                                       std::shared_ptr<ICommandStrategy> command) {
    if (name.empty()) {
        throw std::invalid_argument("Command name cannot be empty");
    }
    m_Commands[name] = std::move(command);
}

std::shared_ptr<ICommandStrategy> CommandRegistry::Resolve(const std::string& name) const {
    auto it = m_Commands.find(name);
    if (it == m_Commands.end()) {
        return nullptr;
    }
    return it->second;
}

bool CommandRegistry::HasCommand(const std::string& name) const {
    return m_Commands.count(name) > 0;
}

std::vector<std::string> CommandRegistry::ListCommands() const {
    std::vector<std::string> names;
    names.reserve(m_Commands.size());
    for (const auto& [name, _] : m_Commands) {
        names.push_back(name);
    }
    return names;
}

std::vector<ToolMetadata> CommandRegistry::ListToolMetadata() const {
    std::vector<ToolMetadata> result;
    result.reserve(m_Commands.size());
    for (const auto& [name, cmd] : m_Commands) {
        ToolMetadata meta = cmd->GetMetadata();
        if (meta.m_Hidden) continue;
        if (meta.m_Name.empty()) {
            meta.m_Name = name;
        }
        if (meta.m_Description.empty()) {
            meta.m_Description = "Execute the " + name + " command";
        }
        if (meta.m_InputSchema.is_null() || meta.m_InputSchema.empty()) {
            meta.m_InputSchema = {
                {"type", "object"},
                {"properties", nlohmann::json::object()},
                {"additionalProperties", true}
            };
        }
        result.push_back(std::move(meta));
    }
    return result;
}
