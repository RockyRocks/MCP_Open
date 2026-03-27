#pragma once
#include <string>
#include <unordered_map>

struct HttpResponse {
    int m_StatusCode = 0;
    std::string m_Body;
    std::unordered_map<std::string, std::string> m_Headers;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual HttpResponse Post(const std::string& url, const std::string& body,
                              const std::unordered_map<std::string, std::string>& headers,
                              int timeoutSeconds = 30) = 0;
    virtual HttpResponse Get(const std::string& url,
                             const std::unordered_map<std::string, std::string>& headers,
                             int timeoutSeconds = 10) = 0;
};
