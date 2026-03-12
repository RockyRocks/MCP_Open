#pragma once
#include "commands/ICommandStrategy.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CommandRegistry {
public:
    void registerCommand(const std::string& name, std::shared_ptr<ICommandStrategy> command);
    std::shared_ptr<ICommandStrategy> resolve(const std::string& name) const;
    bool hasCommand(const std::string& name) const;
    std::vector<std::string> listCommands() const;

private:
    std::unordered_map<std::string, std::shared_ptr<ICommandStrategy>> commands_;
};
