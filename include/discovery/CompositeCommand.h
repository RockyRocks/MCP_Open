#pragma once
#include "commands/ICommandStrategy.h"
#include "discovery/McpServerRegistry.h"
#include "http/IHttpClient.h"
#include <memory>

class CompositeCommand : public ICommandStrategy {
public:
    CompositeCommand(std::shared_ptr<McpServerRegistry> registry,
                     std::shared_ptr<IHttpClient> httpClient);
    std::future<nlohmann::json> executeAsync(const nlohmann::json& request) override;
    ToolMetadata metadata() const override;

private:
    std::shared_ptr<McpServerRegistry> registry_;
    std::shared_ptr<IHttpClient> httpClient_;
};
