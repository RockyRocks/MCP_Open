#include "core/Logger.h"
#include <iostream>

Logger& Logger::getInstance() {
    static Logger lg;
    return lg;
}

void Logger::log(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::cout << "[LOG] " << msg << std::endl;
    if (observer) observer(msg);
}

void Logger::setObserver(std::function<void(const std::string&)> obs) {
    std::lock_guard<std::mutex> lock(mtx_);
    observer = std::move(obs);
}
