#include <core/Logger.h>
#include <iostream>

Logger& Logger::GetInstance() {
    static Logger lg;
    return lg;
}

void Logger::Log(const std::string& msg) {
    std::lock_guard<std::mutex> lock(m_Mtx);
    if (!m_SuppressStdout) {
        std::cout << "[LOG] " << msg << std::endl;
    }
    if (m_Observer) m_Observer(msg);
}

void Logger::SetSuppressStdout(bool suppress) {
    std::lock_guard<std::mutex> lock(m_Mtx);
    m_SuppressStdout = suppress;
}

void Logger::SetObserver(std::function<void(const std::string&)> obs) {
    std::lock_guard<std::mutex> lock(m_Mtx);
    m_Observer = std::move(obs);
}
