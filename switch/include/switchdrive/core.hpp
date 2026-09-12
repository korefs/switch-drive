#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
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

struct Account { std::string id, email, displayName; };
struct RemoteFile {
    std::string id, name, mimeType, md5, resourceKey, shortcutTargetId;
    uint64_t size{};
    bool folder{}, shortcut{}, canDownload{true};
};
struct Task {
    std::string id, accountId, remoteId, displayName, localPath, md5, revision;
    uint64_t expectedSize{}, downloaded{};
    TaskState state{TaskState::Queued};
    LocalState localState{LocalState::NotDownloaded};
    InstallKind installKind{InstallKind::None};
    bool installAfterDownload{}, deleteAfterInstall{true};
    std::string error;
};
struct LibraryItem {
    std::string id, accountId, remoteId, name, localPath, md5;
    uint64_t size{};
    LocalState localState{LocalState::NotDownloaded};
    InstallKind installed{InstallKind::None};
    std::string installedPath, installedContentId;
};
struct State {
    int schemaVersion{1};
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
    bool open(const std::filesystem::path& path, std::string& error);
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
    bool validate(const std::filesystem::path& source, std::string& error) const;
    bool install(const std::filesystem::path& source, std::string& contentId, std::function<bool(uint64_t,uint64_t)> progress, std::string& error) const;
    bool uninstall(const std::string& contentId, std::string& error) const;
};

} // namespace switchdrive
