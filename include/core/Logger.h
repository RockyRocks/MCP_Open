#pragma once
#include <string>
#include <functional>
#include <mutex>

class Logger {
public:
    static Logger& getInstance();
    void log(const std::string& msg);
    void setObserver(std::function<void(const std::string&)> obs);
private:
    Logger() = default;
    std::function<void(const std::string&)> observer;
    std::mutex mtx_;
};
