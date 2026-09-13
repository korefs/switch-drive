#pragma once
#include "switchdrive/core.hpp"
#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace switchdrive {
using ActivityCallback = std::function<bool()>;
bool continueHttpActivity(const ActivityCallback* activity);
enum class DownloadStatus { Completed, AlreadyComplete, Paused, RangeRejected, Failed };

struct DownloadResult {
    DownloadStatus status{DownloadStatus::Failed};
    uint64_t bytesWritten{};
    std::string etag;
    long httpStatus{};
};

struct TransferEstimate {
    double bytesPerSecond{};
    uint64_t etaSeconds{};
    bool ready{};
};

class TransferMeter {
  public:
    using Clock = std::chrono::steady_clock;
    explicit TransferMeter(uint64_t initialBytes, Clock::time_point startedAt = Clock::now());
    TransferEstimate sample(uint64_t received, uint64_t total, Clock::time_point now = Clock::now());
  private:
    uint64_t sampledBytes_{};
    Clock::time_point sampledAt_;
    double smoothedBytesPerSecond_{};
    bool ready_{};
};

std::string formatDataSize(uint64_t bytes);
std::string formatDuration(uint64_t seconds);
std::string formatTransferProgress(uint64_t received, uint64_t total, const TransferEstimate& estimate);

enum class RangeResponse { AcceptBody, AlreadyComplete, Reject };
RangeResponse validateRangeResponse(long status, const std::string& contentRange, uint64_t resumeAt, uint64_t expectedSize);

class HttpClient {
  public:
    struct Response { long status{}; std::string body, etag, contentRange; };
    explicit HttpClient(ActivityCallback activity = {}) : activity_(std::move(activity)) {}
    bool get(const std::string& url, const std::vector<std::string>& headers, Response& out, std::string& error) const;
    bool post(const std::string& url, const std::string& body, const std::vector<std::string>& headers, Response& out, std::string& error) const;
    bool del(const std::string& url, const std::vector<std::string>& headers, Response& out, std::string& error) const;
    bool download(const std::string& url, const std::vector<std::string>& headers, LocalFile& output, uint64_t resumeAt, uint64_t expectedSize, const std::string& ifRange, std::function<bool(const std::string&)> headersAccepted, std::function<bool(uint64_t)> progress, DownloadResult& result, std::string& error) const;
  private:
    ActivityCallback activity_;
};

struct HomeStorageHealth { std::string instanceId, name; int protocolVersion{}, httpPort{8080}; bool authRequired{}; };
struct DiscoveredHomeStorage { std::string baseUrl; HomeStorageHealth health; };
bool normalizeHomeStorageUrl(const std::string& input, std::string& output);
bool parseHomeStorageHealthPayload(const std::string& payload, HomeStorageHealth& health, std::string& error);
bool parseHomeStorageCatalogPayload(const std::string& payload, const std::string& providerId, std::vector<RemoteEntry>& files, std::string& next, std::string& error);
void deduplicateHomeStorageDiscoveries(std::vector<DiscoveredHomeStorage>& results);
class HomeStorageClient {
  public:
    explicit HomeStorageClient(HttpClient http) : http_(std::move(http)) {}
    bool health(const std::string& baseUrl, HomeStorageHealth& health, std::string& error) const;
    bool authenticate(const std::string& baseUrl, const std::string& username, const std::string& password, std::string& token, bool& canManage, std::string& error) const;
    bool list(const ProviderConfig& provider, const std::string& folderId, const std::string& cursor, std::vector<RemoteEntry>& files, std::string& next, std::string& error) const;
    bool hide(const ProviderConfig& provider, const std::string& id, std::string& error) const;
    std::string mediaUrl(const ProviderConfig& provider, const RemoteEntry& file) const;
  private: HttpClient http_;
};
bool discoverHomeStorage(std::vector<DiscoveredHomeStorage>& results, std::string& error);

class AuthClient {
  public:
    AuthClient(HttpClient http, std::string serviceUrl) : http_(std::move(http)), serviceUrl_(std::move(serviceUrl)) {}
    bool begin(const std::string& consoleKey, std::string& id, std::string& url, std::string& qrUrl, std::string& code, std::string& pollSecret, std::string& error) const;
    bool poll(const std::string& id, const std::string& pollSecret, Account& account, std::string& error) const;
    bool claim(const std::string& id, const std::string& pollSecret, std::string& session, Account& account, std::string& error) const;
    bool accounts(const std::string& session, std::vector<Account>& accounts, std::string& error) const;
    bool accessToken(const std::string& session, const std::string& accountId, std::string& token, std::string& error) const;
  private: HttpClient http_; std::string serviceUrl_;
};

class DriveClient {
  public:
    explicit DriveClient(HttpClient http) : http_(std::move(http)) {}
    bool list(const std::string& accessToken, const std::string& folderId, bool sharedWithMe, const std::string& pageToken, std::vector<RemoteEntry>& files, std::string& nextPage, std::string& error) const;
    std::string mediaUrl(const RemoteEntry& file) const;
  private: HttpClient http_;
};
struct DownloadRequest { std::string url; std::vector<std::string> headers; };
struct ProviderCapabilities { bool folders{}, resumableDownloads{}, catalogManage{}, lanDiscovery{}; ChecksumKind checksum{ChecksumKind::None}; };
class IStorageProvider {
  public:
    virtual ~IStorageProvider() = default;
    virtual ProviderCapabilities capabilities() const = 0;
    virtual bool list(const std::string&, bool, const std::string&, std::vector<RemoteEntry>&, std::string&, std::string&) const = 0;
    virtual DownloadRequest downloadRequest(const RemoteEntry&) const = 0;
};
class GoogleStorageProvider final : public IStorageProvider {
  public:
    GoogleStorageProvider(HttpClient http, std::string token) : drive_(std::move(http)), token_(std::move(token)) {}
    ProviderCapabilities capabilities() const override { return {true,true,false,false,ChecksumKind::Md5}; }
    bool list(const std::string&, bool, const std::string&, std::vector<RemoteEntry>&, std::string&, std::string&) const override;
    DownloadRequest downloadRequest(const RemoteEntry&) const override;
  private: DriveClient drive_; std::string token_;
};
class HomeStorageProvider final : public IStorageProvider {
  public:
    HomeStorageProvider(HttpClient http, ProviderConfig config) : home_(std::move(http)), config_(std::move(config)) {}
    ProviderCapabilities capabilities() const override { return {true,true,config_.canManageCatalog,true,ChecksumKind::Sha256}; }
    bool list(const std::string&, bool, const std::string&, std::vector<RemoteEntry>&, std::string&, std::string&) const override;
    DownloadRequest downloadRequest(const RemoteEntry&) const override;
  private: HomeStorageClient home_; ProviderConfig config_;
};
} // namespace switchdrive
