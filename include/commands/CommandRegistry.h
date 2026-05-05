#pragma once
#include <commands/ICommandStrategy.h>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

class CommandRegistry {
public:
    static constexpr int kMaxChainDepth = 5;

    void RegisterCommand(const std::string& name, std::shared_ptr<ICommandStrategy> command);
    std::shared_ptr<ICommandStrategy> Resolve(const std::string& name) const;
    bool HasCommand(const std::string& name) const;
    std::vector<std::string> ListCommands() const;
    std::vector<ToolMetadata> ListToolMetadata() const;

    nlohmann::json ExecuteWithChaining(const std::string& toolName,
                                       const nlohmann::json& request,
                                       int depth = 0);

private:
    mutable std::shared_mutex m_Mutex;
    std::unordered_map<std::string, std::shared_ptr<ICommandStrategy>> m_Commands;
};
