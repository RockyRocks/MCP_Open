#include "commands/CommandRegistry.h"
#include <stdexcept>

void CommandRegistry::registerCommand(const std::string& name,
                                       std::shared_ptr<ICommandStrategy> command) {
    if (name.empty()) {
        throw std::invalid_argument("Command name cannot be empty");
    }
    commands_[name] = std::move(command);
}

std::shared_ptr<ICommandStrategy> CommandRegistry::resolve(const std::string& name) const {
    auto it = commands_.find(name);
    if (it == commands_.end()) {
        return nullptr;
    }
    return it->second;
}

bool CommandRegistry::hasCommand(const std::string& name) const {
    return commands_.count(name) > 0;
}

std::vector<std::string> CommandRegistry::listCommands() const {
    std::vector<std::string> names;
    names.reserve(commands_.size());
    for (const auto& [name, _] : commands_) {
        names.push_back(name);
    }
    return names;
}
