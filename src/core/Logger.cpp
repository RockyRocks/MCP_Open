#include "core/Logger.h"
#include <iostream>

Logger& Logger::getInstance() {
    static Logger lg;
    return lg;
}

void Logger::log(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!suppressStdout_) {
        std::cout << "[LOG] " << msg << std::endl;
    }
    if (observer) observer(msg);
}

void Logger::setSuppressStdout(bool suppress) {
    std::lock_guard<std::mutex> lock(mtx_);
    suppressStdout_ = suppress;
}

void Logger::setObserver(std::function<void(const std::string&)> obs) {
    std::lock_guard<std::mutex> lock(mtx_);
    observer = std::move(obs);
}
