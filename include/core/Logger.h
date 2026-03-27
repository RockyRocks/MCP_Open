#pragma once
#include <string>
#include <functional>
#include <mutex>

class Logger {
public:
    static Logger& GetInstance();
    void Log(const std::string& msg);
    void SetObserver(std::function<void(const std::string&)> obs);
    void SetSuppressStdout(bool suppress);
private:
    Logger() = default;
    std::function<void(const std::string&)> m_Observer;
    std::mutex m_Mtx;
    bool m_SuppressStdout = false;
};
