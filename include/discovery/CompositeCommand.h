#pragma once
#include <commands/ICommandStrategy.h>
#include <discovery/McpServerRegistry.h>
#include <http/IHttpClient.h>
#include <memory>

class CompositeCommand : public ICommandStrategy {
public:
    CompositeCommand(std::shared_ptr<McpServerRegistry> registry,
                     std::shared_ptr<IHttpClient> httpClient);
    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json& request) override;
    ToolMetadata GetMetadata() const override;

private:
    std::shared_ptr<McpServerRegistry> m_Registry;
    std::shared_ptr<IHttpClient> m_HttpClient;
};
