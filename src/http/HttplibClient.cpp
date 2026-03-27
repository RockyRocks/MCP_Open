#include <http/HttplibClient.h>
#include <httplib.h>
#include <stdexcept>
#include <regex>

HttplibClient::UrlParts HttplibClient::ParseUrl(const std::string& url) {
    UrlParts parts;
    // Simple URL parser: scheme://host:port/path
    std::regex urlRegex(R"(^(https?)://([^/:]+)(?::(\d+))?(/.*)?)");
    std::smatch match;
    if (!std::regex_match(url, match, urlRegex)) {
        throw std::invalid_argument("Invalid URL: " + url);
    }
    parts.m_Scheme = match[1].str();
    parts.m_Host = match[2].str();
    parts.m_Port = match[3].matched ? std::stoi(match[3].str())
                                   : (parts.m_Scheme == "https" ? 443 : 80);
    parts.m_Path = match[4].matched ? match[4].str() : "/";
    return parts;
}

HttpResponse HttplibClient::Post(const std::string& url, const std::string& body,
                                  const std::unordered_map<std::string, std::string>& headers,
                                  int timeoutSeconds) {
    auto parts = ParseUrl(url);
    std::string baseUrl = parts.m_Scheme + "://" + parts.m_Host + ":" + std::to_string(parts.m_Port);

    httplib::Client cli(baseUrl);
    cli.set_connection_timeout(timeoutSeconds);
    cli.set_read_timeout(timeoutSeconds);
    cli.set_write_timeout(timeoutSeconds);

    httplib::Headers hlHeaders;
    for (const auto& [k, v] : headers) {
        hlHeaders.emplace(k, v);
    }

    auto res = cli.Post(parts.m_Path, hlHeaders, body, "application/json");

    HttpResponse response;
    if (res) {
        response.m_StatusCode = res->status;
        response.m_Body = res->body;
        for (const auto& [k, v] : res->headers) {
            response.m_Headers[k] = v;
        }
    } else {
        response.m_StatusCode = 0;
        response.m_Body = "Connection failed";
    }
    return response;
}

HttpResponse HttplibClient::Get(const std::string& url,
                                 const std::unordered_map<std::string, std::string>& headers,
                                 int timeoutSeconds) {
    auto parts = ParseUrl(url);
    std::string baseUrl = parts.m_Scheme + "://" + parts.m_Host + ":" + std::to_string(parts.m_Port);

    httplib::Client cli(baseUrl);
    cli.set_connection_timeout(timeoutSeconds);
    cli.set_read_timeout(timeoutSeconds);

    httplib::Headers hlHeaders;
    for (const auto& [k, v] : headers) {
        hlHeaders.emplace(k, v);
    }

    auto res = cli.Get(parts.m_Path, hlHeaders);

    HttpResponse response;
    if (res) {
        response.m_StatusCode = res->status;
        response.m_Body = res->body;
        for (const auto& [k, v] : res->headers) {
            response.m_Headers[k] = v;
        }
    } else {
        response.m_StatusCode = 0;
        response.m_Body = "Connection failed";
    }
    return response;
}
