#include "http/HttplibClient.h"
#include <httplib.h>
#include <stdexcept>
#include <regex>

HttplibClient::UrlParts HttplibClient::parseUrl(const std::string& url) {
    UrlParts parts;
    // Simple URL parser: scheme://host:port/path
    std::regex urlRegex(R"(^(https?)://([^/:]+)(?::(\d+))?(/.*)?)");
    std::smatch match;
    if (!std::regex_match(url, match, urlRegex)) {
        throw std::invalid_argument("Invalid URL: " + url);
    }
    parts.scheme = match[1].str();
    parts.host = match[2].str();
    parts.port = match[3].matched ? std::stoi(match[3].str())
                                   : (parts.scheme == "https" ? 443 : 80);
    parts.path = match[4].matched ? match[4].str() : "/";
    return parts;
}

HttpResponse HttplibClient::post(const std::string& url, const std::string& body,
                                  const std::unordered_map<std::string, std::string>& headers,
                                  int timeoutSeconds) {
    auto parts = parseUrl(url);
    std::string baseUrl = parts.scheme + "://" + parts.host + ":" + std::to_string(parts.port);

    httplib::Client cli(baseUrl);
    cli.set_connection_timeout(timeoutSeconds);
    cli.set_read_timeout(timeoutSeconds);
    cli.set_write_timeout(timeoutSeconds);

    httplib::Headers hlHeaders;
    for (const auto& [k, v] : headers) {
        hlHeaders.emplace(k, v);
    }

    auto res = cli.Post(parts.path, hlHeaders, body, "application/json");

    HttpResponse response;
    if (res) {
        response.statusCode = res->status;
        response.body = res->body;
        for (const auto& [k, v] : res->headers) {
            response.headers[k] = v;
        }
    } else {
        response.statusCode = 0;
        response.body = "Connection failed";
    }
    return response;
}

HttpResponse HttplibClient::get(const std::string& url,
                                 const std::unordered_map<std::string, std::string>& headers,
                                 int timeoutSeconds) {
    auto parts = parseUrl(url);
    std::string baseUrl = parts.scheme + "://" + parts.host + ":" + std::to_string(parts.port);

    httplib::Client cli(baseUrl);
    cli.set_connection_timeout(timeoutSeconds);
    cli.set_read_timeout(timeoutSeconds);

    httplib::Headers hlHeaders;
    for (const auto& [k, v] : headers) {
        hlHeaders.emplace(k, v);
    }

    auto res = cli.Get(parts.path, hlHeaders);

    HttpResponse response;
    if (res) {
        response.statusCode = res->status;
        response.body = res->body;
        for (const auto& [k, v] : res->headers) {
            response.headers[k] = v;
        }
    } else {
        response.statusCode = 0;
        response.body = "Connection failed";
    }
    return response;
}
