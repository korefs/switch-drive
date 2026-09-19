#include "switchdrive/core.hpp"
#include "switchdrive/i18n.hpp"

#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <random>
#include <set>
#include <sstream>
#include <system_error>

#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#endif
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace fs = std::filesystem;
namespace switchdrive {
namespace {

constexpr size_t kFileBufferSize = 256 * 1024;

int syncFile(std::FILE* file) {
#ifdef _WIN32
    return ::_commit(::_fileno(file));
#else
    return ::fsync(::fileno(file));
#endif
}

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

const char* taskKindName(TaskKind kind) { return kind == TaskKind::StreamInstall ? "stream-install" : "download"; }
TaskKind parseTaskKind(const std::string& value) { return value == "stream-install" ? TaskKind::StreamInstall : TaskKind::Download; }

const char* localStateName(LocalState state) {
    switch (state) {
        case LocalState::Present: return "present";
        case LocalState::Missing: return "missing";
        case LocalState::NotDownloaded: return "notDownloaded";
    }
    return "notDownloaded";
}

LocalState parseLocalState(const std::string& value) {
    if (value == "present") return LocalState::Present;
    if (value == "missing") return LocalState::Missing;
    return LocalState::NotDownloaded;
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
const char* providerKindName(ProviderKind kind) { return kind == ProviderKind::GoogleDrive ? "google-drive" : "home-storage"; }
ProviderKind parseProviderKind(const std::string& value) { return value == "google-drive" ? ProviderKind::GoogleDrive : ProviderKind::HomeStorage; }

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

bool normalizeHomeStorageUrl(const std::string& input, std::string& output) {
    const auto begin = input.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return false;
    const auto end = input.find_last_not_of(" \t\r\n");
    const std::string value = input.substr(begin, end - begin + 1);
    std::string scheme, authority;
    if (value.rfind("http://", 0) == 0) { scheme = "http://"; authority = value.substr(7); }
    else if (value.rfind("https://", 0) == 0) { scheme = "https://"; authority = value.substr(8); }
    else {
        if (value.find("://") != std::string::npos) return false;
        authority = value;
    }
    if (authority.empty() || authority.size() > 500 || authority.find_first_of("/@?#\\ \t\r\n") != std::string::npos) return false;
    const auto colon = authority.find(':');
    if (colon != std::string::npos && authority.find(':', colon + 1) != std::string::npos) return false;
    const std::string host = authority.substr(0, colon);
    if (host.empty() || host.front() == '.' || host.back() == '.') return false;
    for (const unsigned char c : host) if (!std::isalnum(c) && c != '.' && c != '-') return false;
    if (colon != std::string::npos) {
        const std::string port = authority.substr(colon + 1);
        if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), [](unsigned char c){ return std::isdigit(c); })) return false;
        const long value = std::strtol(port.c_str(), nullptr, 10); if (value < 1 || value > 65535) return false;
    }
    unsigned octets[4]{}; size_t offset = 0; bool ipv4 = true;
    for (size_t i = 0; i < 4; ++i) {
        const auto dot = host.find('.', offset); const auto stop = i == 3 ? host.size() : dot;
        if (stop == std::string::npos || stop == offset || (i == 3 && dot != std::string::npos)) { ipv4 = false; break; }
        const std::string part = host.substr(offset, stop - offset);
        if (!std::all_of(part.begin(), part.end(), [](unsigned char c){ return std::isdigit(c); })) { ipv4 = false; break; }
        const long n = std::strtol(part.c_str(), nullptr, 10); if (n > 255) { ipv4 = false; break; } octets[i] = static_cast<unsigned>(n); offset = stop + 1;
    }
    const bool numericHost = std::all_of(host.begin(), host.end(), [](unsigned char c){ return std::isdigit(c) || c == '.'; });
    if (numericHost && !ipv4) return false;
    if (scheme.empty()) {
        const bool privateIp = ipv4 && (octets[0] == 10 || octets[0] == 127 || (octets[0] == 192 && octets[1] == 168) || (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) || (octets[0] == 169 && octets[1] == 254));
        const bool localName = host == "localhost" || host.ends_with(".local"); scheme = privateIp || localName ? "http://" : "https://";
    }
    output = scheme + authority;
    return true;
}

bool normalizePairingServiceUrl(const std::string& input, std::string& output) {
    if (!normalizeHomeStorageUrl(input, output) || !output.starts_with("https://")) {
        output.clear();
        return false;
    }
    return true;
}

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
bool isNsz(const std::string& name) { return extensionOf(name) == ".nsz"; }
bool isInstallablePackage(const std::string& name) { return isNsp(name) || isNsz(name); }

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
    if (std::fflush(file_) != 0 || syncFile(file_) != 0) {
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
    if (schemaVersion < 1 || schemaVersion > 9) return state;

    state.schemaVersion = 9;
    state.serviceUrl = stringField(json, "serviceUrl");
    state.consolePublicKey = stringField(json, "consolePublicKey");
    state.sessionToken = stringField(json, "sessionToken");
    state.lastAccountId = stringField(json, "lastAccountId");
    const std::string legacyLastFolderId = stringField(json, "lastFolderId");
    state.activeProviderId = schemaVersion >= 5 ? stringField(json, "activeProviderId") : "google-drive";
    if (state.activeProviderId.empty()) state.activeProviderId = "google-drive";
    state.language = std::string(i18n::languageCode(i18n::parseLanguage(stringField(json, "language"))));

    for (const auto& row : objectRows(json, "accounts")) {
        Account account{stringField(row, "id"), stringField(row, "email"), stringField(row, "displayName")};
        if (!account.id.empty()) state.accounts.push_back(std::move(account));
    }
    if (schemaVersion >= 5) {
        state.providers.clear();
        for (const auto& row : objectRows(json, "providers")) {
            ProviderConfig provider;
            provider.id = stringField(row, "id"); provider.kind = parseProviderKind(stringField(row, "kind"));
            provider.name = stringField(row, "name"); provider.baseUrl = stringField(row, "baseUrl");
            provider.accessToken = stringField(row, "accessToken"); provider.lastFolderId = stringField(row, "lastFolderId");
            provider.canManageCatalog = boolField(row, "canManageCatalog", false);
            if (!provider.id.empty()) state.providers.push_back(std::move(provider));
        }
        if (std::none_of(state.providers.begin(), state.providers.end(), [](const ProviderConfig& p){ return p.kind == ProviderKind::GoogleDrive; })) state.providers.insert(state.providers.begin(),{"google-drive","","","","root",ProviderKind::GoogleDrive,false});
    }
    if (!legacyLastFolderId.empty()) {
        const auto google = std::find_if(state.providers.begin(), state.providers.end(), [](const ProviderConfig& provider) {
            return provider.kind == ProviderKind::GoogleDrive;
        });
        if (google != state.providers.end() && (google->lastFolderId.empty() || google->lastFolderId == "root"))
            google->lastFolderId = legacyLastFolderId;
    }
    const auto google = std::find_if(state.providers.begin(), state.providers.end(), [](const ProviderConfig& provider) {
        return provider.kind == ProviderKind::GoogleDrive;
    });
    if (google != state.providers.end() && (google->lastFolderId.empty() || google->lastFolderId == "selected"))
        google->lastFolderId = "root";
    // Schema 7 sessions were granted drive.file and cannot be reused after the
    // return to drive.readonly. Force a fresh consent without touching local
    // downloads, tasks, providers, language, or the custom service URL.
    if (schemaVersion == 7) {
        state.sessionToken.clear();
        state.lastAccountId.clear();
        state.accounts.clear();
        if (google != state.providers.end()) google->lastFolderId = "root";
    }
    for (const auto& row : objectRows(json, "library")) {
        LibraryItem item;
        item.id = stringField(row, "id");
        item.providerId = schemaVersion >= 5 ? stringField(row, "providerId") : "google-drive";
        item.accountId = stringField(row, "accountId");
        item.remoteId = stringField(row, "remoteId");
        item.name = stringField(row, "name");
        item.localPath = stringField(row, "localPath");
        item.md5 = stringField(row, "md5");
        item.sha256 = stringField(row, "sha256");
        item.size = numberField(row, "size");
        item.localState = parseLocalState(stringField(row, "localState"));
        item.storageKind = stringField(row, "storageKind") == "concatenated" ? StorageKind::Concatenated : StorageKind::Regular;
        // v1-v5 could retain title-install metadata in the Library. In v6 the
        // Library represents package files only, so preserve every possible
        // local package except one explicitly recorded as already deleted.
        if (!item.id.empty() && stringField(row, "localState") != "removedAfterInstall")
            state.library.push_back(std::move(item));
    }
    if (schemaVersion >= 2) {
        for (const auto& row : objectRows(json, "tasks")) {
            Task task;
            task.id = stringField(row, "id");
            task.providerId = schemaVersion >= 5 ? stringField(row, "providerId") : "google-drive";
            task.accountId = stringField(row, "accountId");
            task.remoteId = stringField(row, "remoteId");
            task.displayName = stringField(row, "displayName");
            task.localPath = stringField(row, "localPath");
            task.md5 = stringField(row, "md5");
            task.sha256 = stringField(row, "sha256");
            task.revision = stringField(row, "revision");
            task.etag = stringField(row, "etag");
            task.expectedSize = numberField(row, "expectedSize");
            task.committedBytes = numberField(row, "committedBytes");
            task.state = parseTaskState(stringField(row, "state"));
            task.localState = parseLocalState(stringField(row, "localState"));
            task.storageKind = stringField(row, "storageKind") == "concatenated" ? StorageKind::Concatenated : StorageKind::Regular;
            task.kind = schemaVersion >= 9 ? parseTaskKind(stringField(row, "taskKind")) : TaskKind::Download;
            task.installAfterDownload = boolField(row, "installAfterDownload", false);
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
    output << "{\"schemaVersion\":9,\"serviceUrl\":\"" << escape(state.serviceUrl)
           << "\",\"consolePublicKey\":\"" << escape(state.consolePublicKey)
           << "\",\"sessionToken\":\"" << escape(state.sessionToken)
           << "\",\"lastAccountId\":\"" << escape(state.lastAccountId)
           << "\",\"activeProviderId\":\"" << escape(state.activeProviderId)
           << "\",\"language\":\"" << language << "\",\"accounts\":[";
    for (size_t i = 0; i < state.accounts.size(); ++i) {
        const auto& account = state.accounts[i];
        if (i) output << ',';
        output << "{\"id\":\"" << escape(account.id) << "\",\"email\":\"" << escape(account.email)
               << "\",\"displayName\":\"" << escape(account.displayName) << "\"}";
    }
    output << "],\"providers\":[";
    for (size_t i = 0; i < state.providers.size(); ++i) {
        const auto& provider = state.providers[i]; if (i) output << ',';
        output << "{\"id\":\"" << escape(provider.id) << "\",\"kind\":\"" << providerKindName(provider.kind)
               << "\",\"name\":\"" << escape(provider.name) << "\",\"baseUrl\":\"" << escape(provider.baseUrl)
               << "\",\"accessToken\":\"" << escape(provider.accessToken) << "\",\"lastFolderId\":\"" << escape(provider.lastFolderId)
               << "\",\"canManageCatalog\":" << (provider.canManageCatalog ? "true" : "false") << "}";
    }
    output << "],\"tasks\":[";
    for (size_t i = 0; i < state.tasks.size(); ++i) {
        const auto& task = state.tasks[i];
        if (i) output << ',';
        output << "{\"id\":\"" << escape(task.id) << "\",\"providerId\":\"" << escape(task.providerId) << "\",\"accountId\":\"" << escape(task.accountId)
               << "\",\"remoteId\":\"" << escape(task.remoteId) << "\",\"displayName\":\"" << escape(task.displayName)
               << "\",\"localPath\":\"" << escape(task.localPath) << "\",\"md5\":\"" << escape(task.md5) << "\",\"sha256\":\"" << escape(task.sha256)
               << "\",\"revision\":\"" << escape(task.revision) << "\",\"etag\":\"" << escape(task.etag)
               << "\",\"expectedSize\":" << task.expectedSize << ",\"committedBytes\":" << task.committedBytes
               << ",\"state\":\"" << taskStateName(task.state) << "\",\"localState\":\"" << localStateName(task.localState)
               << "\",\"storageKind\":\"" << storageKindName(task.storageKind)
               << "\",\"taskKind\":\"" << taskKindName(task.kind)
               << "\",\"installAfterDownload\":" << (task.installAfterDownload ? "true" : "false")
               << ",\"error\":\"" << escape(task.error) << "\"}";
    }
    output << "],\"library\":[";
    for (size_t i = 0; i < state.library.size(); ++i) {
        const auto& item = state.library[i];
        if (i) output << ',';
        output << "{\"id\":\"" << escape(item.id) << "\",\"providerId\":\"" << escape(item.providerId) << "\",\"accountId\":\"" << escape(item.accountId)
               << "\",\"remoteId\":\"" << escape(item.remoteId) << "\",\"name\":\"" << escape(item.name)
               << "\",\"localPath\":\"" << escape(item.localPath) << "\",\"md5\":\"" << escape(item.md5) << "\",\"sha256\":\"" << escape(item.sha256)
               << "\",\"size\":" << item.size << ",\"localState\":\"" << localStateName(item.localState)
               << "\",\"storageKind\":\"" << storageKindName(item.storageKind) << "\"}";
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
    const auto version = numberField(json, "journalVersion");
    if (version != 1 && version != 2) { error = i18n::tr(i18n::TextId::JournalInvalid); return false; }
    journal.operation = stringField(json, "operation");
    journal.libraryId = stringField(json, "libraryId");
    journal.localPath = stringField(json, "localPath");
    journal.phase = stringField(json, "phase");
    journal.targetStorage = parseNspStorage(stringField(json, "targetStorage"));
    journal.ticketWasPresent = boolField(json, "ticketWasPresent", false);
    journal.ticketImported = boolField(json, "ticketImported", false);
    journal.streaming = version >= 2 && boolField(json, "streaming", false);
    journal.package.kind = parseNspKind(stringField(json, "kind"));
    journal.package.metaId = stringField(json, "metaId");
    journal.package.baseTitleId = stringField(json, "baseTitleId");
    journal.package.version = static_cast<uint32_t>(numberField(json, "version"));
    for (const auto& row : objectRows(json, "contents")) {
        NspJournalContent content;
        content.id = stringField(row, "id"); content.placeholderId = stringField(row, "placeholderId");
        content.created = boolField(row, "created", false);
        content.completed = version >= 2 && boolField(row, "completed", false);
        content.registered = version >= 2 && boolField(row, "registered", false);
        if (!content.id.empty()) journal.contents.push_back(std::move(content));
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
    data << "{\"journalVersion\":2,\"operation\":\"" << escape(journal.operation) << "\",\"libraryId\":\"" << escape(journal.libraryId)
         << "\",\"localPath\":\"" << escape(journal.localPath) << "\",\"phase\":\"" << escape(journal.phase)
         << "\",\"targetStorage\":\"" << nspStorageName(journal.targetStorage) << "\""
         << ",\"ticketWasPresent\":" << (journal.ticketWasPresent ? "true" : "false") << ",\"ticketImported\":" << (journal.ticketImported ? "true" : "false")
         << ",\"streaming\":" << (journal.streaming ? "true" : "false")
         << ",\"kind\":\"" << nspKindName(journal.package.kind) << "\",\"metaId\":\"" << escape(journal.package.metaId)
         << "\",\"baseTitleId\":\"" << escape(journal.package.baseTitleId) << "\",\"version\":" << journal.package.version << ",\"contents\":[";
    for (size_t i = 0; i < journal.contents.size(); ++i) {
        if (i) data << ',';
        const auto& content = journal.contents[i];
        data << "{\"id\":\"" << escape(content.id) << "\",\"placeholderId\":\"" << escape(content.placeholderId)
             << "\",\"created\":" << (content.created ? "true" : "false")
             << ",\"completed\":" << (content.completed ? "true" : "false")
             << ",\"registered\":" << (content.registered ? "true" : "false") << "}";
    }
    data << "]}";
    const std::string encoded = data.str();
    const bool wrote = std::fwrite(encoded.data(), 1, encoded.size(), output) == encoded.size() && std::fflush(output) == 0 && syncFile(output) == 0;
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

bool StateStore::removeDownload(State& state, const std::string& libraryId, std::string& error) {
    // Copy the id: callers may pass the id of an item that will be erased.
    const std::string id = libraryId;
    const auto item = std::find_if(state.library.begin(), state.library.end(), [&](const LibraryItem& value) { return value.id == id; });
    if (item == state.library.end()) { error = i18n::tr(i18n::TextId::DownloadRecordMissing); return false; }
    NspInstallJournal journal;
    bool pending{};
    if (!loadInstallJournal(journal, error, pending)) return false;
    if (pending && journal.libraryId == id) { error = i18n::tr(i18n::TextId::DownloadRemovalPending); return false; }
    if (!item->localPath.empty()) {
        Task target;
        target.id = id;
        target.displayName = item->name;
        // Never let a stale/corrupt record delete installed content or a directory above its download.
        if (id.empty() || id == "." || id == ".." || fs::path(id).filename() != fs::path(id) ||
            sanitizeFileName(item->name).empty() || sanitizeFileName(item->name) == "." || sanitizeFileName(item->name) == ".." ||
            fs::path(item->localPath).lexically_normal() != downloadPath(target).lexically_normal()) {
            error = i18n::tr(i18n::TextId::DownloadRemovalUnsafePath);
            return false;
        }
        if (!LocalFile::remove(item->localPath, item->storageKind, error)) return false;
    }
    std::erase_if(state.tasks, [&](const Task& task) { return task.id == id; });
    state.library.erase(item);
    return save(state, error);
}

#pragma pack(push, 1)
struct Pfs0Header { char magic[4]; uint32_t fileCount, stringTableSize, reserved; };
struct Pfs0RawEntry { uint64_t offset, size; uint32_t stringOffset, reserved; };
#pragma pack(pop)

bool Pfs0::open(const fs::path& path, StorageKind kind, std::string& error, uint64_t segmentSize) {
    valid_ = false;
    entries_.clear();
    input_.close();
    reader_ = {};
    path_ = path;
    kind_ = kind;
    segmentSize_ = segmentSize;
    if (!input_.open(path, kind, false, error, segmentSize)) return false;
    uint64_t total{};
    if (!input_.size(total, error)) return false;
    reader_ = [this](uint64_t offset, void* buffer, size_t size, std::string& readError) {
        return input_.readAt(offset, buffer, size, readError);
    };
    return open(total, reader_, error);
}

bool Pfs0::open(uint64_t total, Reader reader, std::string& error) {
    valid_ = false;
    entries_.clear();
    if (!reader || total < sizeof(Pfs0Header)) { error = i18n::tr(i18n::TextId::Pfs0InvalidHeader); return false; }
    reader_ = std::move(reader);
    Pfs0Header header{};
    if (!reader_(0, &header, sizeof(header), error) || std::string(header.magic, 4) != "PFS0" || header.fileCount == 0 || header.fileCount > 4096 || header.stringTableSize > 4 * 1024 * 1024) {
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
    if (!reader_(sizeof(header), raw.data(), static_cast<size_t>(rawSize), error) || !reader_(sizeof(header) + rawSize, strings.data(), strings.size(), error)) {
        error = i18n::tr(i18n::TextId::Pfs0InvalidData);
        return false;
    }
    std::set<std::string> names;
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
        std::string parsedName(name, length);
        if (!names.insert(parsedName).second) { error = i18n::tr(i18n::TextId::Pfs0InvalidEntries); return false; }
        entries_.push_back({std::move(parsedName), dataStart + entry.offset, entry.size});
    }
    auto ordered = entries_;
    std::sort(ordered.begin(), ordered.end(), [](const Pfs0Entry& left, const Pfs0Entry& right) { return left.offset < right.offset; });
    for (size_t i = 1; i < ordered.size(); ++i)
        if (ordered[i - 1].offset > UINT64_MAX - ordered[i - 1].size || ordered[i - 1].offset + ordered[i - 1].size > ordered[i].offset) {
            error = i18n::tr(i18n::TextId::Pfs0InvalidEntries); return false;
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
    if (!reader_) { error = i18n::tr(i18n::TextId::Pfs0InvalidData); return false; }
    return reader_(entry.offset + offset, buffer, size, error);
}

} // namespace switchdrive
