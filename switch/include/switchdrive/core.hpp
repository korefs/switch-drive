#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace switchdrive {

enum class TaskState { Queued, Downloading, Paused, Verifying, Installing, Completed, Failed, Cancelled };
enum class LocalState { Present, RemovedAfterInstall, Missing, NotDownloaded };
enum class InstallKind { None, Nro, Nsp };
enum class StorageKind { Regular, Concatenated };

constexpr uint64_t kFat32FileLimit = 4ULL * 1024ULL * 1024ULL * 1024ULL;

struct Account { std::string id, email, displayName; };
struct RemoteFile {
    std::string id, name, mimeType, md5, revision, resourceKey, shortcutTargetId;
    uint64_t size{};
    bool folder{}, shortcut{}, canDownload{true};
};
struct Task {
    std::string id, accountId, remoteId, displayName, localPath, md5, revision, etag;
    uint64_t expectedSize{}, committedBytes{};
    TaskState state{TaskState::Queued};
    LocalState localState{LocalState::NotDownloaded};
    InstallKind installKind{InstallKind::None};
    StorageKind storageKind{StorageKind::Regular};
    bool installAfterDownload{}, deleteAfterInstall{true};
    std::string error;
};
struct LibraryItem {
    std::string id, accountId, remoteId, name, localPath, md5;
    uint64_t size{};
    LocalState localState{LocalState::NotDownloaded};
    InstallKind installed{InstallKind::None};
    StorageKind storageKind{StorageKind::Regular};
    std::string installedPath, installedContentId;
};
struct State {
    int schemaVersion{2};
    std::string serviceUrl, consolePublicKey, sessionToken, lastAccountId, lastFolderId;
    bool deleteAfterInstall{true};
    std::vector<Account> accounts;
    std::vector<Task> tasks;
    std::vector<LibraryItem> library;
};

std::string sanitizeFileName(const std::string& name);
std::string extensionOf(const std::string& name);
bool isNro(const std::string& name);
bool isNsp(const std::string& name);
std::string makeId();
bool fileExists(const std::string& path);
uint64_t fileSize(const std::string& path);
StorageKind storageKindForSize(uint64_t size, uint64_t limit = kFat32FileLimit);
const char* storageKindName(StorageKind kind);

// A logical file can be regular or concatenated. On Switch, concatenated files
// are provided by HOS as one path; the host implementation emulates segments so
// boundary and recovery behavior remains testable without allocating 4 GiB.
class LocalFile {
  public:
    LocalFile() = default;
    ~LocalFile();
    LocalFile(const LocalFile&) = delete;
    LocalFile& operator=(const LocalFile&) = delete;
    LocalFile(LocalFile&& other) noexcept;
    LocalFile& operator=(LocalFile&& other) noexcept;

    bool create(const std::filesystem::path& path, StorageKind kind, std::string& error, uint64_t segmentSize = kFat32FileLimit);
    bool open(const std::filesystem::path& path, StorageKind kind, bool writable, std::string& error, uint64_t segmentSize = kFat32FileLimit);
    bool readAt(uint64_t offset, void* buffer, size_t size, std::string& error) const;
    bool writeAt(uint64_t offset, const void* buffer, size_t size, std::string& error);
    bool size(uint64_t& out, std::string& error) const;
    bool truncate(uint64_t size, std::string& error);
    bool flush(std::string& error);
    void close();

    const std::filesystem::path& path() const { return path_; }
    StorageKind kind() const { return kind_; }
    static bool exists(const std::filesystem::path& path, StorageKind kind);
    static bool remove(const std::filesystem::path& path, StorageKind kind, std::string& error);

  private:
    std::filesystem::path path_;
    StorageKind kind_{StorageKind::Regular};
    uint64_t segmentSize_{kFat32FileLimit};
    bool writable_{};
    std::FILE* file_{};
    bool opened_{};

    std::filesystem::path segmentPath(uint64_t index) const;
    bool openRegular(bool create, std::string& error);
};

class StateStore {
  public:
    explicit StateStore(std::filesystem::path root) : root_(std::move(root)) {}
    State load();
    bool save(const State& state, std::string& error);
    std::filesystem::path downloadPath(const Task& task) const;
  private:
    std::filesystem::path root_;
};

struct Pfs0Entry { std::string name; uint64_t offset{}, size{}; };
class Pfs0 {
  public:
    bool open(const std::filesystem::path& path, StorageKind kind, std::string& error, uint64_t segmentSize = kFat32FileLimit);
    bool open(const std::filesystem::path& path, std::string& error) { return open(path, StorageKind::Regular, error); }
    const std::vector<Pfs0Entry>& entries() const { return entries_; }
    bool valid() const { return valid_; }
  private:
    std::vector<Pfs0Entry> entries_;
    bool valid_{};
};

class NroInstaller {
  public:
    bool validate(const std::filesystem::path& source, std::string& error) const;
    bool install(const std::filesystem::path& source, const std::filesystem::path& destination, bool replace, std::string& error) const;
    bool uninstall(const std::filesystem::path& destination, std::string& error) const;
};

// The NCM adapter has a narrow interface so its journal can be recovered without
// coupling downloads or the UI to system services. Its Switch implementation is
// linked only on console builds.
class NspInstaller {
  public:
    bool validate(const std::filesystem::path& source, StorageKind kind, std::string& error, uint64_t segmentSize = kFat32FileLimit) const;
    bool validate(const std::filesystem::path& source, std::string& error) const { return validate(source, StorageKind::Regular, error); }
    bool install(const std::filesystem::path& source, StorageKind kind, std::string& contentId, std::function<bool(uint64_t,uint64_t)> progress, std::string& error) const;
    bool install(const std::filesystem::path& source, std::string& contentId, std::function<bool(uint64_t,uint64_t)> progress, std::string& error) const { return install(source, StorageKind::Regular, contentId, std::move(progress), error); }
    bool uninstall(const std::string& contentId, std::string& error) const;
};

} // namespace switchdrive
