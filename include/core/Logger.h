#pragma once
#include <string>
#include <functional>
#include <mutex>

class Logger {
public:
    static Logger& getInstance();
    void log(const std::string& msg);
    void setObserver(std::function<void(const std::string&)> obs);
    void setSuppressStdout(bool suppress);
private:
    Logger() = default;
    std::function<void(const std::string&)> observer;
    std::mutex mtx_;
    bool suppressStdout_ = false;
};
