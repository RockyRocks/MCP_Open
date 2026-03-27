#pragma once
#include <string>
#include <vector>

struct McpServerEntry {
    std::string m_Name;
    std::string m_Url;
    std::vector<std::string> m_Capabilities;
    int m_Priority = 1;
    int m_TimeoutSeconds = 30;
};
