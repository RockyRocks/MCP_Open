#pragma once
#include "http/IHttpClient.h"

class HttplibClient : public IHttpClient {
public:
    HttpResponse post(const std::string& url, const std::string& body,
                      const std::unordered_map<std::string, std::string>& headers,
                      int timeoutSeconds = 30) override;
    HttpResponse get(const std::string& url,
                     const std::unordered_map<std::string, std::string>& headers,
                     int timeoutSeconds = 10) override;

private:
    struct UrlParts {
        std::string scheme;
        std::string host;
        int port;
        std::string path;
    };
    static UrlParts parseUrl(const std::string& url);
};
