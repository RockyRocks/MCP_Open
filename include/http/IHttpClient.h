#pragma once
#include <string>
#include <unordered_map>

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual HttpResponse post(const std::string& url, const std::string& body,
                              const std::unordered_map<std::string, std::string>& headers,
                              int timeoutSeconds = 30) = 0;
    virtual HttpResponse get(const std::string& url,
                             const std::unordered_map<std::string, std::string>& headers,
                             int timeoutSeconds = 10) = 0;
};
