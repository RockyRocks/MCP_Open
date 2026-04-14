#pragma once
#include <commands/ICommandStrategy.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CommandRegistry {
public:
    /// Maximum number of automatic chain hops before stopping.
    static constexpr int kMaxChainDepth = 5;

    void RegisterCommand(const std::string& name, std::shared_ptr<ICommandStrategy> command);
    std::shared_ptr<ICommandStrategy> Resolve(const std::string& name) const;
    bool HasCommand(const std::string& name) const;
    std::vector<std::string> ListCommands() const;

    /// Returns metadata for all registered commands, querying each command's GetMetadata().
    /// Fills in name/description/schema defaults for commands that don't override GetMetadata().
    std::vector<ToolMetadata> ListToolMetadata() const;

    /// Execute toolName and automatically follow any optional "chain" field in the
    /// result, dispatching the next tool up to kMaxChainDepth times.
    /// Works across all ToolSource types — detection is at the result level.
    /// Callers always pass depth=0; only ExecuteWithChaining itself passes depth>0.
    nlohmann::json ExecuteWithChaining(const std::string& toolName,
                                       const nlohmann::json& request,
                                       int depth = 0);

private:
    std::unordered_map<std::string, std::shared_ptr<ICommandStrategy>> m_Commands;
};
