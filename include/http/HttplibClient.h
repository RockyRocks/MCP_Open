#pragma once
#include <http/IHttpClient.h>

class HttplibClient : public IHttpClient {
public:
    HttpResponse Post(const std::string& url, const std::string& body,
                      const std::unordered_map<std::string, std::string>& headers,
                      int timeoutSeconds = 30) override;
    HttpResponse Get(const std::string& url,
                     const std::unordered_map<std::string, std::string>& headers,
                     int timeoutSeconds = 10) override;

private:
    struct UrlParts {
        std::string m_Scheme;
        std::string m_Host;
        int m_Port;
        std::string m_Path;
    };
    static UrlParts ParseUrl(const std::string& url);
};
