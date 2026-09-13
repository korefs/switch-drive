#include "switchdrive/core.hpp"
#include "switchdrive/i18n.hpp"

#include <chrono>
#include <climits>
#include <cstring>
#include <random>
#include <sstream>
#include <system_error>

#include <unistd.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace fs = std::filesystem;
namespace switchdrive {
namespace {

constexpr size_t kFileBufferSize = 256 * 1024;

const char* taskStateName(TaskState state) {
    switch (state) {
        case TaskState::Queued: return "queued";
        case TaskState::Downloading: return "downloading";
        case TaskState::Paused: return "paused";
        case TaskState::Verifying: return "verifying";
        case TaskState::Installing: return "installing";
        case TaskState::Completed: return "completed";
        case TaskState::Failed: return "failed";
        case TaskState::Cancelled: return "cancelled";
    }
    return "paused";
}

TaskState parseTaskState(const std::string& value) {
    if (value == "queued") return TaskState::Queued;
    if (value == "downloading") return TaskState::Downloading;
    if (value == "verifying") return TaskState::Verifying;
    if (value == "installing") return TaskState::Installing;
    if (value == "completed") return TaskState::Completed;
    if (value == "failed") return TaskState::Failed;
    if (value == "cancelled") return TaskState::Cancelled;
    return TaskState::Paused;
}

const char* localStateName(LocalState state) {
    switch (state) {
        case LocalState::Present: return "present";
        case LocalState::RemovedAfterInstall: return "removedAfterInstall";
        case LocalState::Missing: return "missing";
        case LocalState::NotDownloaded: return "notDownloaded";
    }
    return "notDownloaded";
}

LocalState parseLocalState(const std::string& value) {
    if (value == "present") return LocalState::Present;
    if (value == "removedAfterInstall") return LocalState::RemovedAfterInstall;
    if (value == "missing") return LocalState::Missing;
    return LocalState::NotDownloaded;
}

const char* installKindName(InstallKind kind) {
    switch (kind) {
        case InstallKind::Nro: return "nro";
        case InstallKind::Nsp: return "nsp";
        case InstallKind::None: return "none";
    }
    return "none";
}

const char* nspKindName(NspContentKind kind) {
    switch (kind) {
        case NspContentKind::BaseGame: return "base";
        case NspContentKind::Update: return "update";
        case NspContentKind::Dlc: return "dlc";
        default: return "unknown";
    }
}
NspContentKind parseNspKind(const std::string& value) {
    if (value == "base") return NspContentKind::BaseGame;
    if (value == "update") return NspContentKind::Update;
    if (value == "dlc") return NspContentKind::Dlc;
    return NspContentKind::Unknown;
}
const char* nspStorageName(NspInstallStorage storage) { return storage == NspInstallStorage::InternalUser ? "internal" : "sd"; }
NspInstallStorage parseNspStorage(const std::string& value) { return value == "internal" ? NspInstallStorage::InternalUser : NspInstallStorage::SdCard; }
const char* nspStateName(NspInstallState state) {
    switch (state) {
        case NspInstallState::Pending: return "pending";
        case NspInstallState::Installing: return "installing";
        case NspInstallState::Installed: return "installed";
        case NspInstallState::Failed: return "failed";
        case NspInstallState::Unverified: return "unverified";
        default: return "none";
    }
}
NspInstallState parseNspState(const std::string& value) {
    if (value == "pending") return NspInstallState::Pending;
    if (value == "installing") return NspInstallState::Installing;
    if (value == "installed") return NspInstallState::Installed;
    if (value == "failed") return NspInstallState::Failed;
    if (value == "unverified") return NspInstallState::Unverified;
    return NspInstallState::None;
}

InstallKind parseInstallKind(const std::string& value) {
    if (value == "nro") return InstallKind::Nro;
    if (value == "nsp") return InstallKind::Nsp;
    return InstallKind::None;
}

std::string escape(const std::string& value) {
    std::string result;
    for (char c : value) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n";
        else result += c;
    }
    return result;
}

bool replaceWithBackup(const fs::path& temporary, const fs::path& current, const fs::path& backup, std::string& error) {
    std::error_code ec;
    const bool hasCurrent = fs::exists(current, ec);
    if (ec) { error = ec.message(); return false; }
    if (hasCurrent) {
        fs::remove(backup, ec);
        if (ec) { error = ec.message(); return false; }
        fs::rename(current, backup, ec);
        if (ec) { error = ec.message(); return false; }
    }
    fs::rename(temporary, current, ec);
    if (ec) { error = ec.message(); return false; }
    return true;
}

std::string stringField(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\":\"";
    const auto start = json.find(marker);
    if (start == std::string::npos) return {};
    std::string result;
    bool escaped = false;
    for (size_t index = start + marker.size(); index < json.size(); ++index) {
        const char c = json[index];
        if (escaped) {
            result += c == 'n' ? '\n' : c;
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            break;
        } else {
            result += c;
        }
    }
    return result;
}

uint64_t numberField(const std::string& json, const std::string& key, uint64_t fallback = 0) {
    const std::string marker = "\"" + key + "\":";
    const auto start = json.find(marker);
    if (start == std::string::npos) return fallback;
    const char* value = json.c_str() + start + marker.size();
    char* end = nullptr;
    const auto parsed = std::strtoull(value, &end, 10);
    return end == value ? fallback : parsed;
}

bool boolField(const std::string& json, const std::string& key, bool fallback) {
    const std::string marker = "\"" + key + "\":";
    const auto start = json.find(marker);
    if (start == std::string::npos) return fallback;
    return json.compare(start + marker.size(), 4, "true") == 0;
}

std::vector<std::string> objectRows(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\":[";
    auto cursor = json.find(marker);
    if (cursor == std::string::npos) return {};
    cursor += marker.size();
    std::vector<std::string> rows;
    while (cursor < json.size()) {
        while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\n' || json[cursor] == ',')) ++cursor;
        if (cursor >= json.size() || json[cursor] == ']') break;
        if (json[cursor] != '{') break;
        const size_t begin = cursor++;
        size_t depth = 1;
        bool inString = false;
        bool escaped = false;
        while (cursor < json.size() && depth) {
            const char c = json[cursor++];
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
            } else if (c == '"') {
                inString = true;
            } else if (c == '{') {
                ++depth;
            } else if (c == '}') {
                --depth;
            }
        }
        if (depth) break;
        rows.push_back(json.substr(begin, cursor - begin));
    }
    return rows;
}

bool seekFile(std::FILE* file, uint64_t offset, std::string& error) {
    if (offset > static_cast<uint64_t>(LLONG_MAX) || ::fseeko(file, static_cast<off_t>(offset), SEEK_SET) != 0) {
        error = i18n::tr(i18n::TextId::SeekFailed);
        return false;
    }
    return true;
}

bool truncateFile(std::FILE* file, uint64_t size, std::string& error) {
    if (size > static_cast<uint64_t>(LLONG_MAX) || ::ftruncate(::fileno(file), static_cast<off_t>(size)) != 0) {
        error = i18n::tr(i18n::TextId::TruncateFailed);
        return false;
    }
    return true;
}

} // namespace

std::string sanitizeFileName(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        if (std::isalnum(c) || c == '.' || c == '_' || c == '-') out += static_cast<char>(c);
        else if (c == ' ') out += '_';
    }
    if (out.empty()) out = "download";
    if (out.size() > 120) out.resize(120);
    return out;
}

std::string extensionOf(const std::string& name) {
    const auto pos = name.find_last_of('.');
    if (pos == std::string::npos) return {};
    std::string ext = name.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}

bool isNro(const std::string& name) { return extensionOf(name) == ".nro"; }
bool isNsp(const std::string& name) { return extensionOf(name) == ".nsp"; }

std::string makeId() {
    static std::mt19937_64 engine{static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count())};
    std::ostringstream output;
    output << std::hex << engine() << engine();
    return output.str();
}

StorageKind storageKindForSize(uint64_t size, uint64_t limit) {
    return size >= limit ? StorageKind::Concatenated : StorageKind::Regular;
}

const char* storageKindName(StorageKind kind) {
    return kind == StorageKind::Concatenated ? "concatenated" : "regular";
}
const char* nspContentKindName(NspContentKind kind) {
    using i18n::TextId;
    switch (kind) {
        case NspContentKind::BaseGame: return i18n::tr(TextId::BaseGame);
        case NspContentKind::Update: return i18n::tr(TextId::Update);
        case NspContentKind::Dlc: return i18n::tr(TextId::Dlc);
        default: return "";
    }
}
const char* nspInstallStorageName(NspInstallStorage storage) {
    return i18n::tr(storage == NspInstallStorage::InternalUser ? i18n::TextId::InternalStorage : i18n::TextId::SdCard);
}
const char* nspInstallStateName(NspInstallState state) { return nspStateName(state); }

NspInstallDecision decideNspInstall(const NspPackageInfo& package, const std::vector<InstalledNspInfo>& installed) {
    if (package.kind == NspContentKind::Unknown || package.metaId.empty()) return NspInstallDecision::Unsupported;
    uint32_t highest = 0;
    bool found = false;
    for (const auto& item : installed) {
        if (!item.present || item.metaId != package.metaId || item.kind != package.kind) continue;
        highest = std::max(highest, item.version);
        found = true;
    }
    if (!found || package.version > highest) return NspInstallDecision::Install;
    return package.version == highest ? NspInstallDecision::AlreadyInstalled : NspInstallDecision::DowngradeBlocked;
}

bool fileExists(const std::string& path) { return LocalFile::exists(path, StorageKind::Regular); }

uint64_t fileSize(const std::string& path) {
    LocalFile file;
    std::string error;
    uint64_t size{};
    if (!file.open(path, StorageKind::Regular, false, error) || !file.size(size, error)) return 0;
    return size;
}

LocalFile::~LocalFile() { close(); }

LocalFile::LocalFile(LocalFile&& other) noexcept { *this = std::move(other); }

LocalFile& LocalFile::operator=(LocalFile&& other) noexcept {
    if (this == &other) return *this;
    close();
    path_ = std::move(other.path_);
    kind_ = other.kind_;
    segmentSize_ = other.segmentSize_;
    writable_ = other.writable_;
    file_ = other.file_;
    opened_ = other.opened_;
    nextWriteOffset_ = other.nextWriteOffset_;
    nextWriteOffsetKnown_ = other.nextWriteOffsetKnown_;
    ioBuffer_ = std::move(other.ioBuffer_);
    other.file_ = nullptr;
    other.opened_ = false;
    other.nextWriteOffsetKnown_ = false;
    return *this;
}

std::filesystem::path LocalFile::segmentPath(uint64_t index) const {
    char name[32]{};
    std::snprintf(name, sizeof(name), "part-%08llu", static_cast<unsigned long long>(index));
    return path_ / name;
}

bool LocalFile::openRegular(bool create, std::string& error) {
    const char* mode = writable_ ? (create ? "w+b" : "r+b") : "rb";
    file_ = std::fopen(path_.string().c_str(), mode);
    if (!file_) {
        error = i18n::tr(i18n::TextId::LogicalFileOpenFailed);
        return false;
    }
    ioBuffer_.clear();
    if (writable_) {
        ioBuffer_.resize(kFileBufferSize);
        if (std::setvbuf(file_, ioBuffer_.data(), _IOFBF, ioBuffer_.size()) != 0) ioBuffer_.clear();
    }
    nextWriteOffsetKnown_ = false;
    opened_ = true;
    return true;
}

bool LocalFile::create(const fs::path& path, StorageKind kind, std::string& error, uint64_t segmentSize) {
    close();
    path_ = path;
    kind_ = kind;
    segmentSize_ = segmentSize;
    writable_ = true;
    if (segmentSize_ == 0) {
        error = i18n::tr(i18n::TextId::InvalidSegmentSize);
        return false;
    }
    std::error_code ec;
    fs::create_directories(path_.parent_path(), ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    if (fs::exists(path_, ec)) {
        error = i18n::tr(i18n::TextId::DownloadAlreadyExists);
        return false;
    }
#ifdef __SWITCH__
    if (kind_ == StorageKind::Concatenated) {
        const Result rc = fsdevCreateFile(path_.string().c_str(), 0, FsCreateOption_BigFile);
        if (R_FAILED(rc)) {
            error = i18n::tr(i18n::TextId::ConcatenatedCreateFailed);
            return false;
        }
        return openRegular(false, error);
    }
#else
    if (kind_ == StorageKind::Concatenated) {
        if (!fs::create_directory(path_, ec) && ec) {
            error = ec.message();
            return false;
        }
        opened_ = true;
        return true;
    }
#endif
    return openRegular(true, error);
}

bool LocalFile::open(const fs::path& path, StorageKind kind, bool writable, std::string& error, uint64_t segmentSize) {
    close();
    path_ = path;
    kind_ = kind;
    segmentSize_ = segmentSize;
    writable_ = writable;
    if (segmentSize_ == 0 || !exists(path_, kind_)) {
        error = i18n::tr(i18n::TextId::LogicalFileMissing);
        return false;
    }
#ifndef __SWITCH__
    if (kind_ == StorageKind::Concatenated) {
        opened_ = true;
        return true;
    }
#endif
    return openRegular(false, error);
}

bool LocalFile::readAt(uint64_t offset, void* buffer, size_t amount, std::string& error) const {
    if (!opened_) {
        error = i18n::tr(i18n::TextId::LogicalFileClosed);
        return false;
    }
#ifndef __SWITCH__
    if (kind_ == StorageKind::Concatenated) {
        auto* target = static_cast<unsigned char*>(buffer);
        size_t remaining = amount;
        while (remaining) {
            const uint64_t part = offset / segmentSize_;
            const uint64_t localOffset = offset % segmentSize_;
            const size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, segmentSize_ - localOffset));
            std::FILE* segment = std::fopen(segmentPath(part).string().c_str(), "rb");
            if (!segment || !seekFile(segment, localOffset, error) || std::fread(target, 1, chunk, segment) != chunk) {
                if (segment) std::fclose(segment);
                if (error.empty()) error = i18n::tr(i18n::TextId::LogicalFileReadFailed);
                return false;
            }
            std::fclose(segment);
            target += chunk;
            offset += chunk;
            remaining -= chunk;
        }
        return true;
    }
#endif
    nextWriteOffsetKnown_ = false;
    if (!seekFile(file_, offset, error)) return false;
    if (std::fread(buffer, 1, amount, file_) != amount) {
        error = i18n::tr(i18n::TextId::LogicalFileReadFailed);
        return false;
    }
    return true;
}

bool LocalFile::writeAt(uint64_t offset, const void* buffer, size_t amount, std::string& error) {
    if (!opened_ || !writable_) {
        error = i18n::tr(i18n::TextId::LogicalFileNotWritable);
        return false;
    }
#ifndef __SWITCH__
    if (kind_ == StorageKind::Concatenated) {
        const auto* source = static_cast<const unsigned char*>(buffer);
        size_t remaining = amount;
        while (remaining) {
            const uint64_t part = offset / segmentSize_;
            const uint64_t localOffset = offset % segmentSize_;
            const size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, segmentSize_ - localOffset));
            const auto segmentPathValue = segmentPath(part);
            std::FILE* segment = std::fopen(segmentPathValue.string().c_str(), "r+b");
            if (!segment) segment = std::fopen(segmentPathValue.string().c_str(), "w+b");
            if (!segment || !seekFile(segment, localOffset, error) || std::fwrite(source, 1, chunk, segment) != chunk) {
                if (segment) std::fclose(segment);
                if (error.empty()) error = i18n::tr(i18n::TextId::SegmentWriteFailed);
                return false;
            }
            if (std::fflush(segment) != 0) {
                std::fclose(segment);
                error = i18n::tr(i18n::TextId::SegmentWriteFailed);
                return false;
            }
            std::fclose(segment);
            source += chunk;
            offset += chunk;
            remaining -= chunk;
        }
        return true;
    }
#endif
    if (!nextWriteOffsetKnown_ || nextWriteOffset_ != offset) {
        if (!seekFile(file_, offset, error)) return false;
    }
    if (std::fwrite(buffer, 1, amount, file_) != amount) {
        error = i18n::tr(i18n::TextId::LogicalFileWriteFailed);
        return false;
    }
    nextWriteOffset_ = offset + amount;
    nextWriteOffsetKnown_ = true;
    return true;
}

bool LocalFile::size(uint64_t& out, std::string& error) const {
    out = 0;
    if (!opened_) {
        error = i18n::tr(i18n::TextId::LogicalFileClosed);
        return false;
    }
#ifndef __SWITCH__
    if (kind_ == StorageKind::Concatenated) {
        for (uint64_t index = 0;; ++index) {
            std::error_code ec;
            const auto part = segmentPath(index);
            if (!fs::exists(part, ec)) return !ec;
            const uint64_t partSize = fs::file_size(part, ec);
            if (ec || partSize > segmentSize_) {
                error = i18n::tr(i18n::TextId::InvalidSegment);
                return false;
            }
            out += partSize;
            if (partSize < segmentSize_) return true;
        }
    }
#endif
    if (::fseeko(file_, 0, SEEK_END) != 0) {
        error = i18n::tr(i18n::TextId::FileSizeQueryFailed);
        return false;
    }
    const off_t position = ::ftello(file_);
    if (position < 0) {
        error = i18n::tr(i18n::TextId::FileSizeQueryFailed);
        return false;
    }
    out = static_cast<uint64_t>(position);
    nextWriteOffset_ = out;
    nextWriteOffsetKnown_ = writable_;
    return true;
}

bool LocalFile::truncate(uint64_t targetSize, std::string& error) {
    if (!opened_ || !writable_) {
        error = i18n::tr(i18n::TextId::LogicalFileNotWritable);
        return false;
    }
#ifndef __SWITCH__
    if (kind_ == StorageKind::Concatenated) {
        const uint64_t fullParts = targetSize / segmentSize_;
        const uint64_t remainder = targetSize % segmentSize_;
        for (uint64_t index = 0;; ++index) {
            const auto part = segmentPath(index);
            std::error_code ec;
            if (!fs::exists(part, ec)) break;
            if (index < fullParts) continue;
            if (index == fullParts && remainder) {
                std::FILE* segment = std::fopen(part.string().c_str(), "r+b");
                if (!segment || !truncateFile(segment, remainder, error)) {
                    if (segment) std::fclose(segment);
                    return false;
                }
                std::fclose(segment);
                continue;
            }
            fs::remove(part, ec);
            if (ec) {
                error = ec.message();
                return false;
            }
        }
        return true;
    }
#endif
    nextWriteOffsetKnown_ = false;
    return truncateFile(file_, targetSize, error);
}

bool LocalFile::flush(std::string& error) {
    if (!opened_) {
        error = i18n::tr(i18n::TextId::LogicalFileClosed);
        return false;
    }
#ifndef __SWITCH__
    if (kind_ == StorageKind::Concatenated) return true;
#endif
    if (std::fflush(file_) != 0 || ::fsync(::fileno(file_)) != 0) {
        error = i18n::tr(i18n::TextId::FileFlushFailed);
        return false;
    }
    return true;
}

void LocalFile::close() {
    if (file_) std::fclose(file_);
    file_ = nullptr;
    opened_ = false;
    nextWriteOffsetKnown_ = false;
    ioBuffer_.clear();
}

bool LocalFile::exists(const fs::path& path, StorageKind kind) {
    std::error_code ec;
#ifndef __SWITCH__
    if (kind == StorageKind::Concatenated) return fs::is_directory(path, ec) && !ec;
#endif
    return fs::is_regular_file(path, ec) && !ec;
}

bool LocalFile::remove(const fs::path& path, StorageKind kind, std::string& error) {
    std::error_code ec;
#ifndef __SWITCH__
    if (kind == StorageKind::Concatenated) {
        fs::remove_all(path, ec);
        if (ec) error = ec.message();
        return !ec;
    }
#endif
    fs::remove(path, ec);
    if (ec) error = ec.message();
    return !ec;
}

State StateStore::load() {
    State state;
    const auto current = root_ / "state.json";
    const auto backup = root_ / "state.json.bak";
    std::ifstream input(current);
    if (!input.good()) input.open(backup);
    if (!input.good()) return state;
    const std::string json((std::istreambuf_iterator<char>(input)), {});
    const int schemaVersion = static_cast<int>(numberField(json, "schemaVersion"));
    if (schemaVersion != 1 && schemaVersion != 2 && schemaVersion != 3 && schemaVersion != 4) return state;

    state.schemaVersion = 4;
    state.serviceUrl = stringField(json, "serviceUrl");
    state.consolePublicKey = stringField(json, "consolePublicKey");
    state.sessionToken = stringField(json, "sessionToken");
    state.lastAccountId = stringField(json, "lastAccountId");
    state.lastFolderId = stringField(json, "lastFolderId");
    state.language = std::string(i18n::languageCode(i18n::parseLanguage(stringField(json, "language"))));
    state.deleteAfterInstall = boolField(json, "deleteAfterInstall", true);

    for (const auto& row : objectRows(json, "accounts")) {
        Account account{stringField(row, "id"), stringField(row, "email"), stringField(row, "displayName")};
        if (!account.id.empty()) state.accounts.push_back(std::move(account));
    }
    for (const auto& row : objectRows(json, "library")) {
        LibraryItem item;
        item.id = stringField(row, "id");
        item.accountId = stringField(row, "accountId");
        item.remoteId = stringField(row, "remoteId");
        item.name = stringField(row, "name");
        item.localPath = stringField(row, "localPath");
        item.md5 = stringField(row, "md5");
        item.size = numberField(row, "size");
        item.localState = parseLocalState(stringField(row, "localState"));
        item.installed = parseInstallKind(stringField(row, "installed"));
        item.storageKind = stringField(row, "storageKind") == "concatenated" ? StorageKind::Concatenated : StorageKind::Regular;
        item.installedPath = stringField(row, "installedPath");
        item.installedContentId = stringField(row, "installedContentId");
        if (schemaVersion >= 3) {
            item.nspContentKind = parseNspKind(stringField(row, "nspContentKind"));
            item.nspStorage = parseNspStorage(stringField(row, "nspStorage"));
            item.nspInstallState = parseNspState(stringField(row, "nspInstallState"));
            item.nspMetaId = stringField(row, "nspMetaId");
            item.nspBaseTitleId = stringField(row, "nspBaseTitleId");
            item.nspVersion = static_cast<uint32_t>(numberField(row, "nspVersion"));
        } else if (item.installed == InstallKind::Nsp) {
            // v2 never recorded enough identity to safely remove installed NSPs.
            item.nspInstallState = NspInstallState::Unverified;
        }
        if (!item.id.empty()) state.library.push_back(std::move(item));
    }
    if (schemaVersion >= 2) {
        for (const auto& row : objectRows(json, "tasks")) {
            Task task;
            task.id = stringField(row, "id");
            task.accountId = stringField(row, "accountId");
            task.remoteId = stringField(row, "remoteId");
            task.displayName = stringField(row, "displayName");
            task.localPath = stringField(row, "localPath");
            task.md5 = stringField(row, "md5");
            task.revision = stringField(row, "revision");
            task.etag = stringField(row, "etag");
            task.expectedSize = numberField(row, "expectedSize");
            task.committedBytes = numberField(row, "committedBytes");
            task.state = parseTaskState(stringField(row, "state"));
            task.localState = parseLocalState(stringField(row, "localState"));
            task.installKind = parseInstallKind(stringField(row, "installKind"));
            task.storageKind = stringField(row, "storageKind") == "concatenated" ? StorageKind::Concatenated : StorageKind::Regular;
            task.installAfterDownload = boolField(row, "installAfterDownload", false);
            task.deleteAfterInstall = boolField(row, "deleteAfterInstall", true);
            task.error = stringField(row, "error");
            if (!task.id.empty()) state.tasks.push_back(std::move(task));
        }
    }
    return state;
}

bool StateStore::save(const State& state, std::string& error) {
    std::error_code ec;
    fs::create_directories(root_, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    const auto temporary = root_ / "state.json.tmp";
    const auto current = root_ / "state.json";
    const auto backup = root_ / "state.json.bak";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = i18n::tr(i18n::TextId::StateTempOpenFailed);
        return false;
    }
    const std::string language(i18n::languageCode(i18n::parseLanguage(state.language)));
    output << "{\"schemaVersion\":4,\"serviceUrl\":\"" << escape(state.serviceUrl)
           << "\",\"consolePublicKey\":\"" << escape(state.consolePublicKey)
           << "\",\"sessionToken\":\"" << escape(state.sessionToken)
           << "\",\"lastAccountId\":\"" << escape(state.lastAccountId)
           << "\",\"lastFolderId\":\"" << escape(state.lastFolderId)
           << "\",\"language\":\"" << language
           << "\",\"deleteAfterInstall\":" << (state.deleteAfterInstall ? "true" : "false") << ",\"accounts\":[";
    for (size_t i = 0; i < state.accounts.size(); ++i) {
        const auto& account = state.accounts[i];
        if (i) output << ',';
        output << "{\"id\":\"" << escape(account.id) << "\",\"email\":\"" << escape(account.email)
               << "\",\"displayName\":\"" << escape(account.displayName) << "\"}";
    }
    output << "],\"tasks\":[";
    for (size_t i = 0; i < state.tasks.size(); ++i) {
        const auto& task = state.tasks[i];
        if (i) output << ',';
        output << "{\"id\":\"" << escape(task.id) << "\",\"accountId\":\"" << escape(task.accountId)
               << "\",\"remoteId\":\"" << escape(task.remoteId) << "\",\"displayName\":\"" << escape(task.displayName)
               << "\",\"localPath\":\"" << escape(task.localPath) << "\",\"md5\":\"" << escape(task.md5)
               << "\",\"revision\":\"" << escape(task.revision) << "\",\"etag\":\"" << escape(task.etag)
               << "\",\"expectedSize\":" << task.expectedSize << ",\"committedBytes\":" << task.committedBytes
               << ",\"state\":\"" << taskStateName(task.state) << "\",\"localState\":\"" << localStateName(task.localState)
               << "\",\"installKind\":\"" << installKindName(task.installKind) << "\",\"storageKind\":\"" << storageKindName(task.storageKind)
               << "\",\"installAfterDownload\":" << (task.installAfterDownload ? "true" : "false")
               << ",\"deleteAfterInstall\":" << (task.deleteAfterInstall ? "true" : "false")
               << ",\"error\":\"" << escape(task.error) << "\"}";
    }
    output << "],\"library\":[";
    for (size_t i = 0; i < state.library.size(); ++i) {
        const auto& item = state.library[i];
        if (i) output << ',';
        output << "{\"id\":\"" << escape(item.id) << "\",\"accountId\":\"" << escape(item.accountId)
               << "\",\"remoteId\":\"" << escape(item.remoteId) << "\",\"name\":\"" << escape(item.name)
               << "\",\"localPath\":\"" << escape(item.localPath) << "\",\"md5\":\"" << escape(item.md5)
               << "\",\"size\":" << item.size << ",\"localState\":\"" << localStateName(item.localState)
               << "\",\"installed\":\"" << installKindName(item.installed) << "\",\"storageKind\":\"" << storageKindName(item.storageKind)
               << "\",\"installedPath\":\"" << escape(item.installedPath) << "\",\"installedContentId\":\"" << escape(item.installedContentId)
               << "\",\"nspContentKind\":\"" << nspKindName(item.nspContentKind) << "\",\"nspStorage\":\"" << nspStorageName(item.nspStorage)
               << "\",\"nspInstallState\":\"" << nspStateName(item.nspInstallState) << "\",\"nspMetaId\":\"" << escape(item.nspMetaId)
               << "\",\"nspBaseTitleId\":\"" << escape(item.nspBaseTitleId) << "\",\"nspVersion\":" << item.nspVersion << "}";
    }
    output << "]}";
    output.flush();
    output.close();
    if (!output) {
        error = i18n::tr(i18n::TextId::StateWriteFailed);
        return false;
    }
    return replaceWithBackup(temporary, current, backup, error);
}

bool StateStore::loadInstallJournal(NspInstallJournal& journal, std::string& error, bool& exists) const {
    journal = {};
    exists = false;
    const auto current = root_ / "install-journal.json";
    const auto backup = root_ / "install-journal.json.bak";
    std::ifstream input(current);
    if (!input.good()) input.open(backup);
    if (!input.good()) return true;
    exists = true;
    const std::string json((std::istreambuf_iterator<char>(input)), {});
    if (numberField(json, "journalVersion") != 1) { error = i18n::tr(i18n::TextId::JournalInvalid); return false; }
    journal.operation = stringField(json, "operation");
    journal.libraryId = stringField(json, "libraryId");
    journal.localPath = stringField(json, "localPath");
    journal.phase = stringField(json, "phase");
    journal.targetStorage = parseNspStorage(stringField(json, "targetStorage"));
    journal.deletePackage = boolField(json, "deletePackage", false);
    journal.ticketWasPresent = boolField(json, "ticketWasPresent", false);
    journal.ticketImported = boolField(json, "ticketImported", false);
    journal.package.kind = parseNspKind(stringField(json, "kind"));
    journal.package.metaId = stringField(json, "metaId");
    journal.package.baseTitleId = stringField(json, "baseTitleId");
    journal.package.version = static_cast<uint32_t>(numberField(json, "version"));
    for (const auto& row : objectRows(json, "contents")) {
        NspJournalContent content;
        content.id = stringField(row, "id"); content.placeholderId = stringField(row, "placeholderId");
        content.created = boolField(row, "created", false);
        if (!content.id.empty()) journal.contents.push_back(std::move(content));
    }
    for (const auto& row : objectRows(json, "previous")) {
        InstalledNspInfo previous;
        previous.present = true;
        previous.metaId = stringField(row, "metaId"); previous.baseTitleId = stringField(row, "baseTitleId");
        previous.version = static_cast<uint32_t>(numberField(row, "version"));
        previous.kind = parseNspKind(stringField(row, "kind")); previous.storage = parseNspStorage(stringField(row, "storage"));
        if (!previous.metaId.empty()) journal.previous.push_back(std::move(previous));
    }
    if (journal.operation.empty() || journal.package.metaId.empty()) { error = i18n::tr(i18n::TextId::JournalIncomplete); return false; }
    return true;
}

bool StateStore::saveInstallJournal(const NspInstallJournal& journal, std::string& error) const {
    std::error_code ec;
    fs::create_directories(root_, ec);
    if (ec) { error = ec.message(); return false; }
    const auto temporary = root_ / "install-journal.json.tmp";
    const auto current = root_ / "install-journal.json";
    const auto backup = root_ / "install-journal.json.bak";
    std::FILE* output = std::fopen(temporary.string().c_str(), "wb");
    if (!output) { error = i18n::tr(i18n::TextId::JournalWriteFailed); return false; }
    std::ostringstream data;
    data << "{\"journalVersion\":1,\"operation\":\"" << escape(journal.operation) << "\",\"libraryId\":\"" << escape(journal.libraryId)
         << "\",\"localPath\":\"" << escape(journal.localPath) << "\",\"phase\":\"" << escape(journal.phase)
         << "\",\"targetStorage\":\"" << nspStorageName(journal.targetStorage) << "\",\"deletePackage\":" << (journal.deletePackage ? "true" : "false")
         << ",\"ticketWasPresent\":" << (journal.ticketWasPresent ? "true" : "false") << ",\"ticketImported\":" << (journal.ticketImported ? "true" : "false")
         << ",\"kind\":\"" << nspKindName(journal.package.kind) << "\",\"metaId\":\"" << escape(journal.package.metaId)
         << "\",\"baseTitleId\":\"" << escape(journal.package.baseTitleId) << "\",\"version\":" << journal.package.version << ",\"contents\":[";
    for (size_t i = 0; i < journal.contents.size(); ++i) {
        if (i) data << ',';
        const auto& content = journal.contents[i];
        data << "{\"id\":\"" << escape(content.id) << "\",\"placeholderId\":\"" << escape(content.placeholderId) << "\",\"created\":" << (content.created ? "true" : "false") << "}";
    }
    data << "],\"previous\":[";
    for (size_t i = 0; i < journal.previous.size(); ++i) {
        if (i) data << ',';
        const auto& previous = journal.previous[i];
        data << "{\"metaId\":\"" << escape(previous.metaId) << "\",\"baseTitleId\":\"" << escape(previous.baseTitleId) << "\",\"version\":" << previous.version << ",\"kind\":\"" << nspKindName(previous.kind) << "\",\"storage\":\"" << nspStorageName(previous.storage) << "\"}";
    }
    data << "]}";
    const std::string encoded = data.str();
    const bool wrote = std::fwrite(encoded.data(), 1, encoded.size(), output) == encoded.size() && std::fflush(output) == 0 && ::fsync(::fileno(output)) == 0;
    std::fclose(output);
    if (!wrote) { error = i18n::tr(i18n::TextId::JournalSyncFailed); return false; }
    if (!replaceWithBackup(temporary, current, backup, error)) return false;
#ifdef __SWITCH__
    const Result rc = fsdevCommitDevice("sdmc");
    if (R_FAILED(rc)) { error = i18n::tr(i18n::TextId::JournalCommitFailed); return false; }
#endif
    return true;
}

bool StateStore::clearInstallJournal(std::string& error) const {
    std::error_code ec;
    fs::remove(root_ / "install-journal.json", ec);
    if (ec) { error = ec.message(); return false; }
    fs::remove(root_ / "install-journal.json.bak", ec);
    if (ec) { error = ec.message(); return false; }
#ifdef __SWITCH__
    if (R_FAILED(fsdevCommitDevice("sdmc"))) { error = i18n::tr(i18n::TextId::JournalClearCommitFailed); return false; }
#endif
    return true;
}

fs::path StateStore::downloadPath(const Task& task) const {
    return root_ / "downloads" / task.id / sanitizeFileName(task.displayName);
}

#pragma pack(push, 1)
struct Pfs0Header { char magic[4]; uint32_t fileCount, stringTableSize, reserved; };
struct Pfs0RawEntry { uint64_t offset, size; uint32_t stringOffset, reserved; };
#pragma pack(pop)

bool Pfs0::open(const fs::path& path, StorageKind kind, std::string& error, uint64_t segmentSize) {
    valid_ = false;
    entries_.clear();
    path_ = path;
    kind_ = kind;
    segmentSize_ = segmentSize;
    LocalFile input;
    if (!input.open(path, kind, false, error, segmentSize)) return false;
    uint64_t total{};
    if (!input.size(total, error)) return false;
    Pfs0Header header{};
    if (!input.readAt(0, &header, sizeof(header), error) || std::string(header.magic, 4) != "PFS0" || header.fileCount == 0 || header.fileCount > 4096 || header.stringTableSize > 4 * 1024 * 1024) {
        error = i18n::tr(i18n::TextId::Pfs0InvalidHeader);
        return false;
    }
    const uint64_t rawSize = static_cast<uint64_t>(header.fileCount) * sizeof(Pfs0RawEntry);
    const uint64_t dataStart = sizeof(header) + rawSize + header.stringTableSize;
    if (dataStart > total) {
        error = i18n::tr(i18n::TextId::Pfs0InvalidEntries);
        return false;
    }
    std::vector<Pfs0RawEntry> raw(header.fileCount);
    std::string strings(header.stringTableSize, '\0');
    if (!input.readAt(sizeof(header), raw.data(), static_cast<size_t>(rawSize), error) || !input.readAt(sizeof(header) + rawSize, strings.data(), strings.size(), error)) {
        error = i18n::tr(i18n::TextId::Pfs0InvalidData);
        return false;
    }
    for (const auto& entry : raw) {
        if (entry.stringOffset >= strings.size() || entry.offset > total - dataStart || entry.size > total - dataStart - entry.offset) {
            error = i18n::tr(i18n::TextId::Pfs0InvalidEntries);
            return false;
        }
        const char* name = strings.data() + entry.stringOffset;
        const size_t max = strings.size() - entry.stringOffset;
        const auto terminator = std::find(name, name + max, '\0');
        const size_t length = static_cast<size_t>(terminator - name);
        if (length == max || length == 0) {
            error = i18n::tr(i18n::TextId::Pfs0InvalidName);
            return false;
        }
        entries_.push_back({std::string(name, length), dataStart + entry.offset, entry.size});
    }
    valid_ = true;
    return true;
}

const Pfs0Entry* Pfs0::find(const std::string& name) const {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Pfs0Entry& entry) { return entry.name == name; });
    return it == entries_.end() ? nullptr : std::addressof(*it);
}

bool Pfs0::read(const Pfs0Entry& entry, uint64_t offset, void* buffer, size_t size, std::string& error) const {
    if (!valid_ || offset > entry.size || size > entry.size - offset) { error = i18n::tr(i18n::TextId::Pfs0ReadOutOfBounds); return false; }
    LocalFile input;
    return input.open(path_, kind_, false, error, segmentSize_) && input.readAt(entry.offset + offset, buffer, size, error);
}

} // namespace switchdrive
