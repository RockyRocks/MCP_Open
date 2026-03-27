#pragma once
#include <commands/ICommandStrategy.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CommandRegistry {
public:
    void RegisterCommand(const std::string& name, std::shared_ptr<ICommandStrategy> command);
    std::shared_ptr<ICommandStrategy> Resolve(const std::string& name) const;
    bool HasCommand(const std::string& name) const;
    std::vector<std::string> ListCommands() const;

    /// Returns metadata for all registered commands, querying each command's GetMetadata().
    /// Fills in name/description/schema defaults for commands that don't override GetMetadata().
    std::vector<ToolMetadata> ListToolMetadata() const;

private:
    std::unordered_map<std::string, std::shared_ptr<ICommandStrategy>> m_Commands;
};
