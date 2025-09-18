#include "ICommandStrategy.h"
#include <nlohmann/json.hpp>
#include <future>
#include <thread>

class EchoCommand : public ICommandStrategy {
public:
    std::future<nlohmann::json> executeAsync(const nlohmann::json& request) override {
        return std::async(std::launch::async, [request]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            nlohmann::json r = { {"status", "ok"}, {"echo", request} };
            return r;
        });
    }
};

std::shared_ptr<ICommandStrategy> createEchoCommand(){
    return std::make_shared<EchoCommand>();
}
