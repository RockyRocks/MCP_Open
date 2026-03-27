#include <commands/ICommandStrategy.h>
#include <nlohmann/json.hpp>
#include <future>
#include <thread>

class EchoCommand : public ICommandStrategy {
public:
    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json& request) override {
        return std::async(std::launch::async, [request]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            nlohmann::json r = {{"status", "ok"}, {"echo", request}};
            return r;
        });
    }

    ToolMetadata GetMetadata() const override {
        return {
            "echo",
            "Echo back the input message",
            {
                {"type", "object"},
                {"properties", {
                    {"message", {{"type", "string"}, {"description", "The message to echo back"}}}
                }},
                {"required", nlohmann::json::array({"message"})}
            },
            "",  // no default model
            {}   // no default parameters
        };
    }
};

std::shared_ptr<ICommandStrategy> CreateEchoCommand() {
    return std::make_shared<EchoCommand>();
}
