#pragma once
#include <string>
#include <vector>

struct McpServerEntry {
    std::string name;
    std::string url;
    std::vector<std::string> capabilities;
    int priority = 1;
    int timeoutSeconds = 30;
};
