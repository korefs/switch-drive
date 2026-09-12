#pragma once
#include "switchdrive/core.hpp"
#include <functional>
#include <string>
#include <vector>

namespace switchdrive {
class HttpClient {
  public:
    struct Response { long status{}; std::string body, etag, contentRange; };
    bool get(const std::string& url, const std::vector<std::string>& headers, Response& out, std::string& error) const;
    bool post(const std::string& url, const std::string& body, const std::vector<std::string>& headers, Response& out, std::string& error) const;
    bool download(const std::string& url, const std::vector<std::string>& headers, const std::string& destination, uint64_t resumeAt, uint64_t expectedSize, std::function<bool(uint64_t)> progress, std::string& error) const;
};

class AuthClient {
  public:
    AuthClient(HttpClient http, std::string serviceUrl) : http_(std::move(http)), serviceUrl_(std::move(serviceUrl)) {}
    bool begin(const std::string& consoleKey, std::string& id, std::string& url, std::string& code, std::string& pollSecret, std::string& error) const;
    bool poll(const std::string& id, const std::string& pollSecret, Account& account, std::string& error) const;
    bool claim(const std::string& id, const std::string& pollSecret, std::string& session, Account& account, std::string& error) const;
    bool accounts(const std::string& session, std::vector<Account>& accounts, std::string& error) const;
    bool accessToken(const std::string& session, const std::string& accountId, std::string& token, std::string& error) const;
  private: HttpClient http_; std::string serviceUrl_;
};

class DriveClient {
  public:
    explicit DriveClient(HttpClient http) : http_(std::move(http)) {}
    bool list(const std::string& accessToken, const std::string& folderId, bool sharedWithMe, const std::string& pageToken, std::vector<RemoteFile>& files, std::string& nextPage, std::string& error) const;
    std::string mediaUrl(const RemoteFile& file) const;
  private: HttpClient http_;
};
} // namespace switchdrive
