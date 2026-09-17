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
enum class ProviderKind { GoogleDrive, HomeStorage };
enum class ChecksumKind { None, Md5, Sha256 };
enum class NspContentKind { Unknown, BaseGame, Update, Dlc };
enum class NspInstallStorage { SdCard, InternalUser };
enum class NspInstallState { None, Pending, Installing, Installed, Failed, Unverified };
enum class NspInstallDecision { Install, AlreadyInstalled, DowngradeBlocked, Unsupported };

constexpr uint64_t kFat32FileLimit = 4ULL * 1024ULL * 1024ULL * 1024ULL;

struct Account { std::string id, email, displayName; };
struct Checksum { ChecksumKind kind{ChecksumKind::None}; std::string value; };
struct RemoteEntry {
    std::string id, providerId, name, mimeType, revision, etag, resourceKey, shortcutTargetId;
    Checksum checksum;
    uint64_t size{};
    bool folder{}, shortcut{}, canDownload{true}, canHide{};
};
struct ProviderConfig {
    std::string id, name, baseUrl, accessToken, lastFolderId;
    ProviderKind kind{ProviderKind::HomeStorage};
    bool canManageCatalog{};
};
struct Task {
    std::string id, providerId, accountId, remoteId, displayName, localPath, md5, sha256, revision, etag;
    uint64_t expectedSize{}, committedBytes{};
    TaskState state{TaskState::Queued};
    LocalState localState{LocalState::NotDownloaded};
    InstallKind installKind{InstallKind::None};
    StorageKind storageKind{StorageKind::Regular};
    bool installAfterDownload{}, deleteAfterInstall{true};
    std::string error;
};
struct LibraryItem {
    std::string id, providerId, accountId, remoteId, name, localPath, md5, sha256;
    uint64_t size{};
    LocalState localState{LocalState::NotDownloaded};
    InstallKind installed{InstallKind::None};
    StorageKind storageKind{StorageKind::Regular};
    std::string installedPath, installedContentId;
    NspContentKind nspContentKind{NspContentKind::Unknown};
    NspInstallStorage nspStorage{NspInstallStorage::SdCard};
    NspInstallState nspInstallState{NspInstallState::None};
    std::string nspMetaId, nspBaseTitleId;
    uint32_t nspVersion{};
};

struct NspContentEntry { std::string id; uint64_t size{}; uint8_t type{}; };
struct NspPackageInfo {
    NspContentKind kind{NspContentKind::Unknown};
    std::string metaId, baseTitleId, metaNcaId;
    uint32_t version{}, requiredSystemVersion{}, requiredApplicationVersion{};
    uint8_t keyGeneration{};
    uint8_t attributes{};
    uint64_t totalInstallBytes{};
    std::vector<NspContentEntry> contents;
    std::vector<uint8_t> extendedHeader;
    bool hasTicket{};
};
struct InstalledNspInfo {
    bool present{};
    NspInstallStorage storage{NspInstallStorage::SdCard};
    uint32_t version{};
    std::string metaId, baseTitleId;
    NspContentKind kind{NspContentKind::Unknown};
};
// This compact journal is intentionally independent of normal library state.
// NCM recovery trusts live metadata, not the phase string.
struct NspJournalContent { std::string id, placeholderId; bool created{}; };
struct NspInstallJournal {
    std::string operation, libraryId, localPath, phase;
    NspPackageInfo package;
    NspInstallStorage targetStorage{NspInstallStorage::SdCard};
    std::vector<NspJournalContent> contents;
    std::vector<InstalledNspInfo> previous;
    bool deletePackage{}, ticketWasPresent{}, ticketImported{};
};
struct State {
    int schemaVersion{5};
    std::string serviceUrl, consolePublicKey, sessionToken, lastAccountId, lastFolderId, activeProviderId{"google-drive"}, language{"en-US"};
    bool deleteAfterInstall{true};
    std::vector<Account> accounts;
    std::vector<ProviderConfig> providers{{"google-drive","","","","root",ProviderKind::GoogleDrive,false}};
    std::vector<Task> tasks;
    std::vector<LibraryItem> library;
};

std::string sanitizeFileName(const std::string& name);
std::string extensionOf(const std::string& name);
bool isNro(const std::string& name);
bool isNsp(const std::string& name);
bool isNsz(const std::string& name);
bool isInstallablePackage(const std::string& name);
bool normalizeHomeStorageUrl(const std::string& input, std::string& output);
std::string makeId();
bool fileExists(const std::string& path);
uint64_t fileSize(const std::string& path);
StorageKind storageKindForSize(uint64_t size, uint64_t limit = kFat32FileLimit);
const char* storageKindName(StorageKind kind);
const char* nspContentKindName(NspContentKind kind);
const char* nspInstallStorageName(NspInstallStorage storage);
const char* nspInstallStateName(NspInstallState state);
NspInstallDecision decideNspInstall(const NspPackageInfo& package, const std::vector<InstalledNspInfo>& installed);

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
    mutable uint64_t nextWriteOffset_{};
    mutable bool nextWriteOffsetKnown_{};
    std::vector<char> ioBuffer_;

    std::filesystem::path segmentPath(uint64_t index) const;
    bool openRegular(bool create, std::string& error);
};

class StateStore {
  public:
    explicit StateStore(std::filesystem::path root) : root_(std::move(root)) {}
    State load();
    bool save(const State& state, std::string& error);
    bool removeDownload(State& state, const std::string& libraryId, std::string& error);
    bool loadInstallJournal(NspInstallJournal& journal, std::string& error, bool& exists) const;
    bool saveInstallJournal(const NspInstallJournal& journal, std::string& error) const;
    bool clearInstallJournal(std::string& error) const;
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
    const Pfs0Entry* find(const std::string& name) const;
    bool read(const Pfs0Entry& entry, uint64_t offset, void* buffer, size_t size, std::string& error) const;
    bool valid() const { return valid_; }
  private:
    std::vector<Pfs0Entry> entries_;
    bool valid_{};
    std::filesystem::path path_;
    StorageKind kind_{StorageKind::Regular};
    uint64_t segmentSize_{kFat32FileLimit};
    mutable LocalFile input_;
};

using NczSink = std::function<bool(uint64_t, const void*, size_t, std::string&)>;
bool inspectNcz(const Pfs0& pfs0, const Pfs0Entry& entry, uint64_t& decompressedSize, std::string& error);
bool streamNcz(const Pfs0& pfs0, const Pfs0Entry& entry, const NczSink& sink, std::string& error);

class NroInstaller {
  public:
    bool validate(const std::filesystem::path& source, std::string& error) const;
    bool install(const std::filesystem::path& source, const std::filesystem::path& destination, bool replace, std::string& error) const;
    bool uninstall(const std::filesystem::path& destination, std::string& error) const;
};

// The caller owns the mounted filesystem. Implementations own and close their
// directory/file handles, including on failure. Results use Horizon encoding.
class CnmtFileReader {
  public:
    virtual ~CnmtFileReader() = default;
    virtual uint32_t openDirectory() = 0;
    virtual uint32_t nextFile(std::string& name, bool& end) = 0;
    virtual uint32_t openFile(const std::string& absolutePath) = 0;
    virtual uint32_t fileSize(int64_t& size) = 0;
    virtual uint32_t readFile(void* data, size_t size, uint64_t& bytesRead) = 0;
};
bool readCnmtFile(CnmtFileReader& reader, std::vector<uint8_t>& bytes, std::string& error);

// The NCM adapter has a narrow interface so its journal can be recovered without
// coupling downloads or the UI to system services. Its Switch implementation is
// linked only on console builds.
class NspInstaller {
  public:
    bool validate(const std::filesystem::path& source, StorageKind kind, std::string& error, uint64_t segmentSize = kFat32FileLimit) const;
    bool validate(const std::filesystem::path& source, std::string& error) const { return validate(source, StorageKind::Regular, error); }
    bool inspect(const std::filesystem::path& source, StorageKind kind, NspPackageInfo& info, std::string& error, uint64_t segmentSize = kFat32FileLimit) const;
    bool parseCnmt(const void* data, size_t size, NspPackageInfo& info, std::string& error) const;
    bool queryInstalled(const NspPackageInfo& package, std::vector<InstalledNspInfo>& installed, std::string& error) const;
    bool install(const std::filesystem::path& source, StorageKind kind, const NspPackageInfo& package, NspInstallStorage destination, StateStore& store, NspInstallJournal& journal, std::function<bool(uint64_t,uint64_t)> progress, std::string& error) const;
    bool recover(StateStore& store, NspInstallJournal& journal, bool& installCommitted, std::string& error) const;
    bool uninstall(const InstalledNspInfo& target, StateStore& store, NspInstallJournal& journal, std::string& error) const;
    bool install(const std::filesystem::path& source, StorageKind kind, std::string& contentId, std::function<bool(uint64_t,uint64_t)> progress, std::string& error) const;
    bool install(const std::filesystem::path& source, std::string& contentId, std::function<bool(uint64_t,uint64_t)> progress, std::string& error) const { return install(source, StorageKind::Regular, contentId, std::move(progress), error); }
    bool uninstall(const std::string& contentId, std::string& error) const;
};

} // namespace switchdrive
