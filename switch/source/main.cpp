#include "switchdrive/core.hpp"
#include "switchdrive/i18n.hpp"
#include "switchdrive/network.hpp"
#include "switchdrive/ui.hpp"

#include <switch.h>
#include <mbedtls/md5.h>
#include <mbedtls/sha256.h>
#include <curl/curl.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;
using namespace switchdrive;
using namespace switchdrive::i18n;

namespace {

#define hidScanInput() ui::scanInput()
#define hidKeysDown(_unused) ui::keysDown()
#define CONTROLLER_P1_AUTO 0
#define printf(...) ui::writef(__VA_ARGS__)
#define consoleClear() ui::clear()
#define consoleUpdate(_unused) ui::present()

bool networkReady{};
uint32_t networkResult{};
uint32_t networkTcpRxSize{};
uint32_t networkTcpRxMaxSize{};
bool networkSocketFallback{};

constexpr const char* kRoot = "sdmc:/switch-drive";
constexpr const char* kDefaultService = "";
constexpr uint64_t kCheckpointBytes = 64ULL * 1024ULL * 1024ULL;
constexpr auto kCheckpointInterval = std::chrono::seconds(10);
constexpr auto kTransferInputInterval = std::chrono::milliseconds(50);
constexpr auto kTransferUiInterval = std::chrono::milliseconds(250);
std::chrono::steady_clock::time_point lastNetworkInputAt{};
std::chrono::steady_clock::time_point lastNetworkUiAt{};
bool networkCancelRequested{};
void title(const char* page) {
    consoleClear();
    ui::instance().setHeader(page);
}

std::string networkError() {
    char message[256]{};
    std::snprintf(message, sizeof(message), tr(TextId::NetworkUnavailable), networkResult);
    return message;
}

std::string accountName(const State& state) {
    for (const auto& account : state.accounts) if (account.id == state.lastAccountId) return account.email;
    return tr(TextId::NoAccountConnected);
}

ProviderConfig* providerById(State& state, const std::string& id) {
    for (auto& provider : state.providers) if (provider.id == id) return &provider;
    return nullptr;
}

std::string activeProviderName(State& state) {
    auto* provider = providerById(state, state.activeProviderId);
    return !provider || provider->kind == ProviderKind::GoogleDrive ? tr(TextId::MyDrive) : provider->name;
}

std::string fileSize(uint64_t bytes) {
    char text[64]{};
    std::snprintf(text, sizeof(text), tr(TextId::FileSize), static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

void hint(const char* text) { ui::instance().setHint(text); }

void mainTitle(int page) {
    static constexpr std::array<TextId, 4> pages{TextId::Home, TextId::Files, TextId::Library, TextId::Settings};
    consoleClear();
    std::vector<std::string> tabs;
    tabs.reserve(pages.size());
    for (const auto item : pages) tabs.emplace_back(tr(item));
    ui::instance().setHeader(tr(pages[static_cast<size_t>(page)]), tabs, page);
}

void mainHint(const char* text) { hint(text); }

void waitForButton() {
    printf("\n%s", tr(TextId::Continue));
    while (appletMainLoop()) {
        hidScanInput();
        if (hidKeysDown(CONTROLLER_P1_AUTO) & HidNpadButton_A) return;
        consoleUpdate(nullptr);
    }
}

bool pumpUi() {
    const auto now = std::chrono::steady_clock::now();
    if (now - lastNetworkInputAt >= kTransferInputInterval) {
        lastNetworkInputAt = now;
        hidScanInput();
        networkCancelRequested = networkCancelRequested || (hidKeysDown(CONTROLLER_P1_AUTO) & HidNpadButton_B);
    }
    if (now - lastNetworkUiAt >= kTransferUiInterval) {
        lastNetworkUiAt = now;
        consoleUpdate(nullptr);
    }
    return appletMainLoop() && !networkCancelRequested;
}

HttpClient activeHttp() {
    lastNetworkInputAt = {};
    lastNetworkUiAt = {};
    networkCancelRequested = false;
    return HttpClient{pumpUi};
}

bool chooseNspDestination(const NspPackageInfo& package, const std::vector<InstalledNspInfo>& installed, NspInstallStorage& destination) {
    destination = NspInstallStorage::SdCard;
    while (appletMainLoop()) {
        title(tr(TextId::InstallNsp));
        char version[128]{};
        std::snprintf(version, sizeof(version), tr(TextId::Version), package.version);
        std::string summary = std::string(nspContentKindName(package.kind)) + " · " + package.baseTitleId + " · " + version;
        if (!installed.empty()) {
            std::snprintf(version, sizeof(version), tr(TextId::InstalledVersion), installed.front().version, nspInstallStorageName(installed.front().storage));
            summary += std::string(" · ") + version;
        }
        ui::instance().setSubtitle(summary);
        ui::instance().setRows({{tr(TextId::SdCard), package.baseTitleId, ui::Icon::File}, {tr(TextId::InternalStorage), package.baseTitleId, ui::Icon::File}}, destination == NspInstallStorage::SdCard ? 0 : 1);
        hint(tr(TextId::DestinationHint));
        hidScanInput(); const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        const int touched = ui::instance().takeRowSelection();
        if (touched >= 0) destination = touched == 0 ? NspInstallStorage::SdCard : NspInstallStorage::InternalUser;
        if (pressed & (HidNpadButton_Up | HidNpadButton_Down)) destination = destination == NspInstallStorage::SdCard ? NspInstallStorage::InternalUser : NspInstallStorage::SdCard;
        if (pressed & HidNpadButton_A) return true;
        if (pressed & HidNpadButton_B) return false;
        consoleUpdate(nullptr);
    }
    return false;
}

std::string configServiceUrl() {
    std::ifstream input(std::string(kRoot) + "/config.json");
    const std::string config((std::istreambuf_iterator<char>(input)), {});
    const std::string marker = "\"service_url\":\"";
    const auto start = config.find(marker);
    if (start == std::string::npos) return kDefaultService;
    const auto begin = start + marker.size();
    const auto end = config.find('"', begin);
    return end == std::string::npos ? "" : config.substr(begin, end - begin);
}

void saveOrShow(StateStore& store, const State& state) {
    std::string error;
    if (!store.save(state, error)) { printf("\n"); printf(tr(TextId::SaveFailed), error.c_str()); }
}

bool md5File(const fs::path& path, StorageKind kind, std::string& digest, std::string& error) {
    LocalFile file;
    uint64_t size{};
    if (!file.open(path, kind, false, error) || !file.size(size, error)) return false;
    mbedtls_md5_context context;
    mbedtls_md5_init(&context);
    mbedtls_md5_starts(&context);
    std::array<unsigned char, 64 * 1024> buffer{};
    for (uint64_t offset = 0; offset < size;) {
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(buffer.size(), size - offset));
        if (!file.readAt(offset, buffer.data(), chunk, error)) {
            mbedtls_md5_free(&context);
            return false;
        }
        mbedtls_md5_update(&context, buffer.data(), chunk);
        offset += chunk;
        ui::instance().setProgress(offset, size);
        if (!pumpUi()) {
            mbedtls_md5_free(&context);
            error = tr(TextId::OperationCancelled);
            return false;
        }
    }
    std::array<unsigned char, 16> raw{};
    mbedtls_md5_finish(&context, raw.data());
    mbedtls_md5_free(&context);
    char output[33]{};
    for (size_t i = 0; i < raw.size(); ++i) std::snprintf(output + i * 2, 3, "%02x", raw[i]);
    digest = output;
    return true;
}

bool sha256File(const fs::path& path, StorageKind kind, std::string& digest, std::string& error) {
    LocalFile file; uint64_t size{}; if (!file.open(path, kind, false, error) || !file.size(size, error)) return false;
    mbedtls_sha256_context context; mbedtls_sha256_init(&context); mbedtls_sha256_starts(&context, 0);
    std::array<unsigned char, 64 * 1024> buffer{};
    for (uint64_t offset = 0; offset < size;) { const size_t chunk = static_cast<size_t>(std::min<uint64_t>(buffer.size(), size - offset)); if (!file.readAt(offset, buffer.data(), chunk, error)) { mbedtls_sha256_free(&context); return false; } mbedtls_sha256_update(&context, buffer.data(), chunk); offset += chunk; ui::instance().setProgress(offset, size); if (!pumpUi()) { mbedtls_sha256_free(&context); error = tr(TextId::OperationCancelled); return false; } }
    std::array<unsigned char, 32> raw{}; mbedtls_sha256_finish(&context, raw.data()); mbedtls_sha256_free(&context); char output[65]{}; for (size_t i = 0; i < raw.size(); ++i) std::snprintf(output + i * 2, 3, "%02x", raw[i]); digest = output; return true;
}

bool connectAccount(StateStore& store, State& state) {
    title(tr(TextId::ConnectDrive));
    if (!networkReady) { printf("%s\n", networkError().c_str()); waitForButton(); return false; }
    if (state.serviceUrl.empty()) {
        printf("%s\n", tr(TextId::ConfigMissing));
        waitForButton();
        return false;
    }
    if (state.consolePublicKey.empty()) state.consolePublicKey = makeId() + makeId();
    AuthClient auth(activeHttp(), state.serviceUrl);
    std::string id, url, qrUrl, code, pollSecret, error;
    if (!auth.begin(state.consolePublicKey, id, url, qrUrl, code, pollSecret, error)) {
        printf(tr(TextId::StartFailed), error.c_str()); printf("\n");
        waitForButton();
        return false;
    }
    ui::instance().setSubtitle(tr(TextId::ScanWithPhone));
    ui::instance().setQrCode(qrUrl);
    printf("%s\n\x1b[36m%s\x1b[0m\n\n%s: \x1b[33;1m%s\x1b[0m\n", tr(TextId::OpenOnPhone), url.c_str(), tr(TextId::Code), code.c_str());
    hint(tr(TextId::CheckNow));
    while (appletMainLoop()) {
        hidScanInput();
        const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        if (pressed & HidNpadButton_B) return false;
        if (pressed & HidNpadButton_A) {
            Account account;
            if (auth.poll(id, pollSecret, account, error) && auth.claim(id, pollSecret, state.sessionToken, account, error)) {
                const auto exists = std::find_if(state.accounts.begin(), state.accounts.end(), [&](const Account& value) { return value.id == account.id; });
                if (exists == state.accounts.end()) state.accounts.push_back(account);
                state.lastAccountId = account.id;
                saveOrShow(store, state);
                printf("\n"); printf(tr(TextId::Connected), account.email.c_str()); printf("\n");
                waitForButton();
                return true;
            }
            printf("\n%s", error.c_str());
        }
        consoleUpdate(nullptr);
    }
    return false;
}

bool acquireToken(const State& state, std::string& token, std::string& error) {
    if (!networkReady) { error = networkError(); return false; }
    if (state.lastAccountId.empty()) {
        error = tr(TextId::ConnectAccountFirst);
        return false;
    }
    return AuthClient(activeHttp(), state.serviceUrl).accessToken(state.sessionToken, state.lastAccountId, token, error);
}

bool sameRemote(const Task& task, const RemoteEntry& remote, const std::string& accountId) {
    const bool checksumMatches = remote.checksum.kind == ChecksumKind::Sha256 ? task.sha256 == remote.checksum.value : remote.checksum.kind == ChecksumKind::Md5 ? task.md5 == remote.checksum.value : task.md5.empty() && task.sha256.empty();
    return task.providerId == remote.providerId && task.accountId == accountId && task.remoteId == remote.id && task.revision == remote.revision && task.expectedSize == remote.size && checksumMatches;
}

bool hasResumeIdentity(const Task& task) {
    return !task.providerId.empty() && !task.remoteId.empty() && !task.revision.empty() && task.expectedSize > 0 && !task.localPath.empty();
}

Task* findIncompleteTask(State& state, const std::string& providerId, const std::string& accountId, const std::string& remoteId) {
    for (auto& task : state.tasks) {
        if (task.providerId == providerId && task.accountId == accountId && task.remoteId == remoteId && task.state != TaskState::Completed && task.state != TaskState::Cancelled) return &task;
    }
    return nullptr;
}

enum class ResumeChoice { Resume, Restart, Cancel };

ResumeChoice askResumeChoice(const Task& task, bool sourceChanged, bool identityMissing) {
    title(tr(sourceChanged ? TextId::RemoteChanged : TextId::PartialDownloadFound));
    printf("%s\n", task.displayName.c_str());
    printf(tr(TextId::BytesConfirmed), static_cast<unsigned long long>(task.committedBytes), static_cast<unsigned long long>(task.expectedSize)); printf("\n");
    if (sourceChanged) {
        printf("%s\n", tr(TextId::RemoteVersionChanged));
        hint(tr(TextId::RestartCancelHint));
    } else if (identityMissing) {
        printf("%s\n", tr(TextId::PartialIdentityMissing));
        hint(tr(TextId::RestartCancelHint));
    } else {
        hint(tr(TextId::ResumeRestartCancelHint));
    }
    while (appletMainLoop()) {
        hidScanInput();
        const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        if (pressed & HidNpadButton_X) return ResumeChoice::Restart;
        if (!sourceChanged && !identityMissing && (pressed & HidNpadButton_A)) return ResumeChoice::Resume;
        if (pressed & HidNpadButton_B) return ResumeChoice::Cancel;
        consoleUpdate(nullptr);
    }
    return ResumeChoice::Cancel;
}

bool reconcileTask(Task& task, std::string& error) {
    if (!LocalFile::exists(task.localPath, task.storageKind)) {
        task.committedBytes = 0;
        task.localState = LocalState::NotDownloaded;
        return true;
    }
    LocalFile file;
    uint64_t physicalSize{};
    if (!file.open(task.localPath, task.storageKind, true, error) || !file.size(physicalSize, error)) return false;
    if (physicalSize > task.expectedSize) {
        task.state = TaskState::Failed;
        task.error = tr(TextId::PartialTooLarge);
        return false;
    }
    if (physicalSize > task.committedBytes) {
        if (!file.truncate(task.committedBytes, error) || !file.flush(error)) return false;
    } else if (physicalSize < task.committedBytes) {
        task.committedBytes = physicalSize;
    }
    task.localState = task.committedBytes ? LocalState::Present : LocalState::NotDownloaded;
    return true;
}

bool checkpointTask(StateStore& store, State& state, Task& task, DownloadWriter& writer, std::string& error) {
    uint64_t size{};
    if (!writer.checkpoint(size, error)) return false;
    if (size > task.expectedSize) {
        error = tr(TextId::PartialTooLarge);
        return false;
    }
    task.committedBytes = size;
    task.localState = size ? LocalState::Present : LocalState::NotDownloaded;
    return store.save(state, error);
}

bool hasEnoughSpace(uint64_t needed) {
    std::error_code ec;
    const auto space = fs::space("sdmc:/", ec);
    return !ec && space.available >= needed;
}

void applyRemote(Task& task, const RemoteEntry& remote, const std::string& accountId, const StateStore& store) {
    task.providerId = remote.providerId.empty() ? "google-drive" : remote.providerId;
    task.accountId = accountId;
    task.remoteId = remote.id;
    task.displayName = remote.name;
    task.expectedSize = remote.size;
    task.md5 = remote.checksum.kind == ChecksumKind::Md5 ? remote.checksum.value : "";
    task.sha256 = remote.checksum.kind == ChecksumKind::Sha256 ? remote.checksum.value : "";
    task.revision = remote.revision;
    task.etag.clear();
    task.storageKind = storageKindForSize(remote.size);
    task.localPath = store.downloadPath(task).string();
    task.committedBytes = 0;
    task.localState = LocalState::NotDownloaded;
    task.error.clear();
}

bool verifyAndRecord(StateStore& store, State& state, Task& task, std::string& error) {
    task.state = TaskState::Verifying;
    saveOrShow(store, state);
    std::string digest;
    const bool useSha256 = !task.sha256.empty();
    if (!(useSha256 ? sha256File(task.localPath, task.storageKind, digest, error) : md5File(task.localPath, task.storageKind, digest, error))) return false;
    if ((useSha256 && digest != task.sha256) || (!useSha256 && !task.md5.empty() && digest != task.md5)) {
        error = tr(TextId::ChecksumMismatch);
        return false;
    }
    task.state = TaskState::Completed;
    task.committedBytes = task.expectedSize;
    task.localState = LocalState::Present;
    const auto existing = std::find_if(state.library.begin(), state.library.end(), [&](const LibraryItem& item) { return item.id == task.id; });
    if (existing == state.library.end()) {
        LibraryItem item;
        item.id = task.id;
        item.providerId = task.providerId;
        item.accountId = task.accountId;
        item.remoteId = task.remoteId;
        item.name = task.displayName;
        item.localPath = task.localPath;
        item.md5 = task.md5;
        item.sha256 = task.sha256;
        item.size = task.expectedSize;
        item.localState = LocalState::Present;
        item.storageKind = task.storageKind;
        state.library.push_back(std::move(item));
    }
    return store.save(state, error);
}

void installDownloaded(StateStore& store, State& state, Task& task) {
    auto library = std::find_if(state.library.begin(), state.library.end(), [&](const LibraryItem& item) { return item.id == task.id; });
    if (library == state.library.end()) {
        printf(tr(TextId::InstallFailed), tr(TextId::DownloadRecordMissing)); printf("\n");
        return;
    }
    std::string error;
    bool installed = false;
    if (isNro(task.displayName)) {
        NroInstaller installer;
        const std::string baseName = sanitizeFileName(task.displayName.substr(0, task.displayName.size() - 4));
        const fs::path location = std::string("sdmc:/switch/") + baseName + "/" + sanitizeFileName(task.displayName);
        installed = installer.install(task.localPath, location, false, error);
        if (installed) {
            library->installed = InstallKind::Nro;
            library->installedPath = location.string();
        }
    } else if (isInstallablePackage(task.displayName)) {
        NspInstaller installer;
        NspPackageInfo package;
        std::vector<InstalledNspInfo> existing;
        installed = installer.inspect(task.localPath, task.storageKind, package, error) && installer.queryInstalled(package, existing, error);
        if (installed) {
            const auto decision = decideNspInstall(package, existing);
            if (decision == NspInstallDecision::DowngradeBlocked) { installed = false; error = tr(TextId::DowngradeBlocked); }
            else if (decision == NspInstallDecision::AlreadyInstalled) {
                library->installed = InstallKind::Nsp; library->nspContentKind = package.kind; library->nspMetaId = package.metaId; library->nspBaseTitleId = package.baseTitleId; library->nspVersion = package.version; library->nspInstallState = NspInstallState::Installed;
            } else {
                NspInstallStorage destination;
                if (!chooseNspDestination(package, existing, destination)) { printf("\n%s\n", tr(TextId::InstallCancelled)); return; }
                NspInstallJournal journal; journal.libraryId = library->id; journal.localPath = task.localPath; journal.deletePackage = task.deleteAfterInstall;
                library->nspInstallState = NspInstallState::Installing; saveOrShow(store, state);
                installed = installer.install(task.localPath, task.storageKind, package, destination, store, journal, [](uint64_t current, uint64_t total) { ui::instance().setProgress(current, total); printf("\r"); printf(tr(TextId::InstallingBytes), static_cast<unsigned long long>(current), static_cast<unsigned long long>(total)); consoleUpdate(nullptr); return appletMainLoop(); }, error);
                if (installed) {
                    library->installed = InstallKind::Nsp; library->installedContentId = package.metaId; library->nspContentKind = package.kind; library->nspStorage = destination; library->nspMetaId = package.metaId; library->nspBaseTitleId = package.baseTitleId; library->nspVersion = package.version; library->nspInstallState = NspInstallState::Installed;
                } else library->nspInstallState = NspInstallState::Failed;
            }
        }
        if (installed) {
            library->nspInstallState = NspInstallState::Installed;
        }
    } else {
        printf(tr(TextId::InstallFailed), tr(TextId::UnsupportedInstallType)); printf("\n");
        return;
    }
    if (!installed) {
        if (error.empty()) error = tr(TextId::InstallFailureUnknown);
        printf(tr(TextId::InstallFailed), error.c_str()); printf("\n");
        return;
    }
    if (task.deleteAfterInstall) {
        if (LocalFile::remove(task.localPath, task.storageKind, error)) {
            task.localState = LocalState::RemovedAfterInstall;
            library->localState = LocalState::RemovedAfterInstall;
        } else {
            printf(tr(TextId::InstalledCleanupPending), error.c_str()); printf("\n");
        }
    }
    saveOrShow(store, state);
    printf("\n%s\n", tr(TextId::InstallComplete));
    if (library->installed == InstallKind::Nsp && library->nspContentKind != NspContentKind::BaseGame) printf("%s\n", tr(TextId::NonBaseHomeHint));
}

void downloadFile(StateStore& store, State& state, const RemoteEntry& remote, bool installAfter) {
    title(tr(TextId::Transfers));
    printf(tr(TextId::Downloading), remote.name.c_str()); printf("\n%s\n", tr(TextId::PreparingDownload));
    consoleUpdate(nullptr);
    std::string token, error;
    const std::string providerId = remote.providerId.empty() ? "google-drive" : remote.providerId;
    const bool homeStorage = providerId != "google-drive";
    ProviderConfig* home = homeStorage ? providerById(state, providerId) : nullptr;
    if (homeStorage && !home) { printf("\n%s", tr(TextId::ConfigMissing)); waitForButton(); return; }
    if (!homeStorage && !acquireToken(state, token, error)) {
        printf("\n%s", error.c_str());
        waitForButton();
        return;
    }

    const std::string sourceAccount = homeStorage ? "" : state.lastAccountId;
    Task* task = findIncompleteTask(state, providerId, sourceAccount, remote.id);
    bool restart = false;
    if (task) {
        const bool changed = !sameRemote(*task, remote, sourceAccount);
        const bool missingIdentity = !hasResumeIdentity(*task);
        if (changed || missingIdentity || task->committedBytes) {
            const ResumeChoice choice = askResumeChoice(*task, changed, missingIdentity);
            if (choice == ResumeChoice::Cancel) return;
            restart = choice == ResumeChoice::Restart;
        }
    } else {
        Task newTask;
        newTask.id = makeId();
        newTask.deleteAfterInstall = state.deleteAfterInstall;
        applyRemote(newTask, remote, sourceAccount, store);
        state.tasks.push_back(std::move(newTask));
        task = &state.tasks.back();
    }

    if (restart) {
        if (LocalFile::exists(task->localPath, task->storageKind) && !LocalFile::remove(task->localPath, task->storageKind, error)) {
            title(tr(TextId::RestartDownload));
            printf(tr(TextId::CannotDeletePartial), error.c_str()); printf("\n");
            waitForButton();
            return;
        }
        applyRemote(*task, remote, sourceAccount, store);
    }
    const bool installRequested = task->installAfterDownload || installAfter;
    task->installAfterDownload = installRequested;
    task->deleteAfterInstall = state.deleteAfterInstall;
    task->state = TaskState::Queued;
    task->error.clear();

    if (LocalFile::exists(task->localPath, task->storageKind)) {
        if (!reconcileTask(*task, error)) {
            task->state = TaskState::Failed;
            task->error = error;
            saveOrShow(store, state);
            title(tr(TextId::InvalidPartial));
            printf("%s\n", error.c_str());
            waitForButton();
            return;
        }
    }
    if (!hasEnoughSpace(task->expectedSize - task->committedBytes)) {
        task->state = TaskState::Paused;
        task->error = tr(TextId::SdCardInsufficient);
        saveOrShow(store, state);
        title(tr(TextId::InsufficientSpace));
        printf("%s\n", tr(TextId::FreeSpaceAndRetry));
        waitForButton();
        return;
    }

    LocalFile output;
    if (LocalFile::exists(task->localPath, task->storageKind)) {
        if (!output.open(task->localPath, task->storageKind, true, error)) {
            task->state = TaskState::Failed;
            task->error = error;
            saveOrShow(store, state);
            return;
        }
    } else if (!output.create(task->localPath, task->storageKind, error)) {
        task->state = TaskState::Failed;
        task->error = error;
        saveOrShow(store, state);
        title(tr(TextId::StorageError));
        printf("%s\n", error.c_str());
        waitForButton();
        return;
    }

    task->state = TaskState::Downloading;
    saveOrShow(store, state);
    title(tr(task->committedBytes ? TextId::ResumingDownload : TextId::Transfers));
    ui::instance().setProgress(task->committedBytes, task->expectedSize);
    printf(tr(TextId::Downloading), task->displayName.c_str()); printf("\n");
    if (installRequested) printf("%s\n", tr(TextId::InstallAfterDownloadQueued));
    hint(tr(TextId::DownloadPauseHint));
    printf("%s", formatTransferProgress(task->committedBytes, task->expectedSize, {}).c_str());
    consoleUpdate(nullptr);
    TransferMeter transferMeter(task->committedBytes);
    auto lastProgressAt = std::chrono::steady_clock::time_point{};
    auto lastCheckpoint = task->committedBytes;
    auto lastCheckpointAt = std::chrono::steady_clock::now();
    DownloadResult result;
    DownloadWriter writer(output, task->committedBytes);
    const DownloadRequest request = homeStorage && home ? HomeStorageProvider(activeHttp(), *home).downloadRequest(remote) : GoogleStorageProvider(activeHttp(), token).downloadRequest(remote);
    bool downloaded{};
    if (task->committedBytes == task->expectedSize) {
        uint64_t durable{};
        downloaded = writer.finish(durable, error) && durable == task->expectedSize;
        result.bytesReceived = result.bytesWritten = result.bytesDurable = durable;
        result.writer = writer.stats();
        result.status = downloaded ? DownloadStatus::AlreadyComplete : DownloadStatus::Failed;
    } else {
        downloaded = activeHttp().download(
            request.url,
            request.headers,
            writer,
            task->committedBytes,
            task->expectedSize,
            task->etag,
            [&](const std::string& etag) {
                task->etag = etag;
                return store.save(state, error);
            },
            [&](uint64_t received) {
                const auto now = std::chrono::steady_clock::now();
                if (received == task->expectedSize || now - lastProgressAt >= kTransferUiInterval) {
                    ui::instance().setProgress(received, task->expectedSize);
                    const std::string progress = formatTransferProgress(received, task->expectedSize, transferMeter.sample(received, task->expectedSize, now));
                    printf("\r%s   ", progress.c_str());
                    lastProgressAt = now;
                }
                if (received - lastCheckpoint >= kCheckpointBytes && now - lastCheckpointAt >= kCheckpointInterval) {
                    if (!checkpointTask(store, state, *task, writer, error)) return false;
                    lastCheckpoint = task->committedBytes;
                    lastCheckpointAt = now;
                }
                return appletMainLoop();
            },
            result,
            error);
    }

    uint64_t finalDurable{};
    std::string finalizationError;
    if (!writer.finish(finalDurable, finalizationError)) {
        downloaded = false;
        result.status = DownloadStatus::Failed;
        if (error.empty()) error = finalizationError;
    }
    result.writer = writer.stats();
    result.bytesWritten = result.writer.writtenBytes;
    result.bytesDurable = finalDurable;
    std::string checkpointError;
    if (!checkpointTask(store, state, *task, writer, checkpointError)) {
        downloaded = false;
        result.status = DownloadStatus::Failed;
        if (error.empty()) error = checkpointError;
    }
    char transferDiagnostic[320]{};
    std::snprintf(transferDiagnostic, sizeof(transferDiagnostic), tr(TextId::DownloadPerformanceDiagnostic),
        ui::instance().appletMode() ? 1 : 0, networkTcpRxSize, networkTcpRxMaxSize, networkSocketFallback ? 1 : 0,
        static_cast<unsigned long long>(result.bytesReceived), static_cast<unsigned long long>(result.bytesWritten),
        static_cast<unsigned long long>(result.bytesDurable), static_cast<unsigned long long>(result.totalMicroseconds),
        static_cast<unsigned long long>(result.writer.producerWaitMicroseconds), static_cast<unsigned long long>(result.writer.writeMicroseconds),
        static_cast<unsigned long long>(result.writer.peakQueuedBytes), result.writer.asynchronous ? 1 : 0);
    ui::instance().diagnostic(transferDiagnostic);
    output.close();
    if (!downloaded) {
        task->state = result.status == DownloadStatus::Paused ? TaskState::Paused : TaskState::Failed;
        task->error = error;
        saveOrShow(store, state);
        printf("\n%s: %s\n", tr(result.status == DownloadStatus::RangeRejected ? TextId::RangeRejected : TextId::Paused), error.c_str());
        waitForButton();
        return;
    }

    printf("\n%s\n", tr(TextId::VerifyingDownload));
    hint(tr(TextId::Cancel));
    ui::instance().setProgress(0, task->expectedSize);
    consoleUpdate(nullptr);
    if (!verifyAndRecord(store, state, *task, error)) {
        task->state = TaskState::Failed;
        task->error = error;
        saveOrShow(store, state);
        printf("\n"); printf(tr(TextId::DownloadInvalid), error.c_str()); printf("\n");
        waitForButton();
        return;
    }
    printf("\n%s\n", tr(TextId::DownloadComplete));
    hint("");
    consoleUpdate(nullptr);
    if (installRequested) installDownloaded(store, state, *task);
    waitForButton();
}

void recoverTasks(StateStore& store, State& state) {
    bool changed = false;
    for (auto& task : state.tasks) {
        if (task.state == TaskState::Completed || task.state == TaskState::Cancelled) continue;
        task.state = TaskState::Paused;
        if (!hasResumeIdentity(task)) {
            task.error = tr(TextId::PartialIdentityRestart);
            changed = true;
            continue;
        }
        std::string error;
        if (!reconcileTask(task, error)) {
            task.state = TaskState::Failed;
            task.error = error;
        } else if (task.committedBytes == task.expectedSize) {
            task.state = TaskState::Paused;
            task.error = tr(TextId::CompleteAwaitingVerification);
        } else {
            task.error = tr(TextId::InterruptedDownload);
        }
        changed = true;
    }
    if (changed) saveOrShow(store, state);
}

void recoverInstallJournal(StateStore& store, State& state) {
    NspInstallJournal journal; std::string error; bool exists = false;
    if (!store.loadInstallJournal(journal, error, exists)) { title(tr(TextId::NspRecovery)); printf("%s\n%s\n", error.c_str(), tr(TextId::NoNspInstallWillStart)); waitForButton(); return; }
    if (!exists) return;
    const NspInstallJournal recovered = journal;
    NspInstaller installer;
    if (!installer.recover(store, journal, error)) { title(tr(TextId::NspRecovery)); printf("%s\n", error.c_str()); waitForButton(); return; }
    const auto it = std::find_if(state.library.begin(), state.library.end(), [&](const LibraryItem& item) { return item.id == recovered.libraryId; });
    if (it != state.library.end() && recovered.operation == "install") it->nspInstallState = recovered.phase == "committed" ? NspInstallState::Installed : NspInstallState::Failed;
    saveOrShow(store, state);
}

bool promptText(const char* label, std::string& value, bool password = false) {
#ifdef __SWITCH__
    SwkbdConfig keyboard; if (R_FAILED(swkbdCreate(&keyboard, 0))) return false;
    if (password) swkbdConfigMakePresetPassword(&keyboard); else swkbdConfigMakePresetDefault(&keyboard); swkbdConfigSetHeaderText(&keyboard, label);
    swkbdConfigSetInitialText(&keyboard, value.c_str());
    std::array<char, 512> buffer{}; const Result result = swkbdShow(&keyboard, buffer.data(), buffer.size()); swkbdClose(&keyboard);
    if (R_FAILED(result)) return false;
    value = buffer.data();
    return !value.empty();
#else
    (void)label; (void)value; (void)password; return false;
#endif
}

bool selectDiscovered(const std::vector<DiscoveredHomeStorage>& found, size_t& selected) {
    selected = 0;
    while (appletMainLoop()) {
        title(tr(TextId::DetectNetwork)); std::vector<ui::Row> rows; for (const auto& item : found) rows.push_back({item.health.name, item.baseUrl, ui::Icon::Cloud}); ui::instance().setRows(std::move(rows), selected); hint(tr(TextId::NavigationHint));
        hidScanInput(); const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO); const int touched = ui::instance().takeRowSelection(); if (touched >= 0 && static_cast<size_t>(touched) < found.size()) selected = static_cast<size_t>(touched);
        if (pressed & HidNpadButton_Down) selected = (selected + 1) % found.size();
        if (pressed & HidNpadButton_Up) selected = (selected + found.size() - 1) % found.size();
        if (pressed & HidNpadButton_A) return true;
        if (pressed & HidNpadButton_B) return false;
        consoleUpdate(nullptr);
    }
    return false;
}

void configureHomeStorage(StateStore& store, State& state) {
    if (!networkReady) { title(tr(TextId::HomeStorage)); printf("%s\n", networkError().c_str()); waitForButton(); return; }
    size_t choice = 0; std::string address;
    while (appletMainLoop()) {
        title(tr(TextId::HomeStorage)); ui::instance().setRows({{tr(TextId::DetectNetwork), tr(TextId::DetectingStorage), ui::Icon::Cloud}, {tr(TextId::ManualSetup), tr(TextId::ServerAddress), ui::Icon::Settings}}, choice); hint(tr(TextId::NavigationHint));
        hidScanInput(); const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO); const int touched = ui::instance().takeRowSelection(); if (touched >= 0) choice = static_cast<size_t>(touched); if (pressed & (HidNpadButton_Up | HidNpadButton_Down)) choice = 1 - choice; if (pressed & HidNpadButton_B) return;
        if (pressed & HidNpadButton_A) {
            if (choice == 0) { title(tr(TextId::DetectNetwork)); printf("%s\n", tr(TextId::DetectingStorage)); consoleUpdate(nullptr); std::vector<DiscoveredHomeStorage> found, validated; std::string error; if (discoverHomeStorage(found, error)) for (const auto& candidate : found) { HomeStorageHealth checked; if (HomeStorageClient(activeHttp()).health(candidate.baseUrl, checked, error) && checked.instanceId == candidate.health.instanceId) validated.push_back({candidate.baseUrl,checked}); } if (validated.empty()) { printf("%s\n", tr(TextId::NoStorageFound)); waitForButton(); return; } size_t selected{}; if (!selectDiscovered(validated, selected)) return; address = validated[selected].baseUrl; }
            else if (!promptText(tr(TextId::ServerAddress), address) || !normalizeHomeStorageUrl(address, address)) { title(tr(TextId::HomeStorage)); printf("%s\n", tr(TextId::InvalidAddress)); waitForButton(); return; }
            break;
        }
        consoleUpdate(nullptr);
    }
    HomeStorageClient client(activeHttp()); HomeStorageHealth health; std::string error; if (!client.health(address, health, error)) { title(tr(TextId::HomeStorage)); printf("%s\n", error.c_str()); waitForButton(); return; }
    std::string token;bool canManage=false;if (health.authRequired) { std::string username, password; if (!promptText(tr(TextId::Username), username) || !promptText(tr(TextId::Password), password, true)) return; if (!client.authenticate(address, username, password, token, canManage, error)) { title(tr(TextId::HomeStorage)); printf("%s\n", error.c_str()); waitForButton(); return; } }
    const std::string id = "home-" + health.instanceId; auto* existing = providerById(state, id); if (!existing) { state.providers.push_back({}); existing = &state.providers.back(); }
    existing->id=id; existing->kind=ProviderKind::HomeStorage; existing->name=health.name; existing->baseUrl=address; existing->accessToken=token; existing->lastFolderId="root";existing->canManageCatalog=canManage; state.activeProviderId=id; saveOrShow(store,state);
    title(tr(TextId::HomeStorage)); printf(tr(TextId::ProviderConnected), health.name.c_str()); printf("\n"); waitForButton();
}

bool confirmHideHome(const ProviderConfig& provider, const RemoteEntry& file) {
    title(tr(TextId::HideCatalogEntry)); printf("%s\n\n%s\n", file.name.c_str(), tr(TextId::HideCatalogConfirm)); hint(tr(TextId::RemoveConfirm)); consoleUpdate(nullptr);
    while(appletMainLoop()){hidScanInput();const auto pressed=hidKeysDown(CONTROLLER_P1_AUTO);if(pressed&HidNpadButton_B)return false;if(pressed&HidNpadButton_X){std::string error;if(!HomeStorageClient(activeHttp()).hide(provider,file.id,error)){printf("\n%s\n",error.c_str());waitForButton();}return true;}consoleUpdate(nullptr);}return false;
}

void browseHome(StateStore& store, State& state, ProviderConfig& provider) {
    std::string folder=provider.lastFolderId.empty()?"root":provider.lastFolderId;std::vector<std::string> parents;if(folder!="root")parents.push_back("root");size_t selected=0;bool reload=true;std::vector<RemoteEntry> files;std::string next,error;HomeStorageProvider home(activeHttp(),provider);
    while(appletMainLoop()){
        if(reload){files.clear();next.clear();if(!home.list(folder,false,"",files,next,error)){title(tr(TextId::HomeStorage));printf("%s\n",error.c_str());waitForButton();return;}selected=0;reload=false;}
        title(provider.name.c_str());if(files.empty())printf("%s\n",tr(TextId::EmptyFolder));std::vector<ui::Row> rows;for(const auto& file:files)rows.push_back({file.name,file.folder?tr(TextId::Folder):fileSize(file.size),file.folder?ui::Icon::Folder:ui::Icon::File});ui::instance().setRows(std::move(rows),selected);hint(tr(TextId::HomeBrowseHint));
        hidScanInput();const auto pressed=hidKeysDown(CONTROLLER_P1_AUTO);const int touched=ui::instance().takeRowSelection();if(touched>=0&&static_cast<size_t>(touched)<files.size())selected=static_cast<size_t>(touched);
        if(pressed&HidNpadButton_B){if(parents.empty())break;folder=parents.back();parents.pop_back();reload=true;continue;}if(files.empty()){consoleUpdate(nullptr);continue;}
        if(pressed&HidNpadButton_Down){if(selected+1<files.size())++selected;else if(!next.empty()){std::vector<RemoteEntry> page;std::string following;if(!home.list(folder,false,next,page,following,error)){printf("%s\n",error.c_str());waitForButton();return;}files.insert(files.end(),page.begin(),page.end());next=following;if(selected+1<files.size())++selected;}}
        if(pressed&HidNpadButton_Up)selected=(selected+files.size()-1)%files.size();
        auto& file=files[selected];
        if((pressed&HidNpadButton_A)&&file.folder){parents.push_back(folder);folder=file.id;reload=true;}
        if((pressed&(HidNpadButton_X|HidNpadButton_Y))&&file.canDownload&&!file.folder){downloadFile(store,state,file,(pressed&HidNpadButton_Y)!=0);reload=true;}
        if((pressed&HidNpadButton_ZL)&&file.canHide){if(confirmHideHome(provider,file))reload=true;}
        consoleUpdate(nullptr);
    }
    provider.lastFolderId=folder;saveOrShow(store,state);
}

void browse(StateStore& store, State& state);
void chooseStorageProvider(StateStore& store, State& state) {
    size_t selected=0;while(appletMainLoop()){title(tr(TextId::StorageProviders));std::vector<ui::Row> rows;for(const auto& p:state.providers)rows.push_back({p.kind==ProviderKind::GoogleDrive?tr(TextId::MyDrive):p.name,p.kind==ProviderKind::GoogleDrive?accountName(state):p.baseUrl,ui::Icon::Cloud});ui::instance().setRows(std::move(rows),selected);hint(tr(TextId::NavigationHint));hidScanInput();const auto pressed=hidKeysDown(CONTROLLER_P1_AUTO);const int touched=ui::instance().takeRowSelection();if(touched>=0&&static_cast<size_t>(touched)<state.providers.size())selected=static_cast<size_t>(touched);if(pressed&HidNpadButton_Down)selected=(selected+1)%state.providers.size();if(pressed&HidNpadButton_Up)selected=(selected+state.providers.size()-1)%state.providers.size();if(pressed&HidNpadButton_B)return;if(pressed&HidNpadButton_A){auto& provider=state.providers[selected];state.activeProviderId=provider.id;if(provider.kind==ProviderKind::GoogleDrive)browse(store,state);else browseHome(store,state,provider);return;}consoleUpdate(nullptr);}
}

void browse(StateStore& store, State& state) {
    std::string token, error;
    if (!acquireToken(state, token, error)) {
        title(tr(TextId::Files));
        printf("%s\n", error.c_str());
        waitForButton();
        return;
    }
    GoogleStorageProvider drive(activeHttp(), token);
    std::string folder = state.lastFolderId.empty() ? "root" : state.lastFolderId;
    std::vector<std::string> parents;
    bool shared = false;
    size_t selected = 0;
    std::vector<RemoteEntry> files;
    std::string next;
    bool reload = true;
    while (appletMainLoop()) {
        if (reload) {
            files.clear();
            next.clear();
            if (!drive.list(folder, shared, "", files, next, error)) {
                title(tr(TextId::Files));
                printf(tr(TextId::DriveError), error.c_str()); printf("\n");
                waitForButton();
                return;
            }
            selected = 0;
            reload = false;
        }
        title(tr(shared ? TextId::SharedWithMe : TextId::MyDrive));
        ui::instance().setSubtitle(accountName(state));
        if (files.empty()) printf("%s\n", tr(TextId::EmptyFolder));
        std::vector<ui::Row> rows;
        for (const auto& file : files) rows.push_back({file.name, file.folder ? tr(TextId::Folder) : fileSize(file.size), file.folder ? ui::Icon::Folder : ui::Icon::File});
        ui::instance().setRows(std::move(rows), selected);
        hint(tr(TextId::BrowseHint));
        bool refresh = false;
        while (appletMainLoop() && !refresh) {
            hidScanInput();
            const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO);
            const int touched = ui::instance().takeRowSelection();
            if (touched >= 0 && static_cast<size_t>(touched) < files.size()) { selected = static_cast<size_t>(touched); refresh = true; }

            if (pressed & HidNpadButton_Down) {
                if (!files.empty()) {
                    if (selected + 1 < files.size()) { ++selected; refresh = true; }
                    else if (!next.empty()) {
                        std::vector<RemoteEntry> page;
                        std::string following;
                        if (!drive.list(folder, shared, next, page, following, error)) { title(tr(TextId::Files)); printf(tr(TextId::DriveError), error.c_str()); printf("\n"); waitForButton(); return; }
                        files.insert(files.end(), page.begin(), page.end());
                        next = following;
                        if (selected + 1 < files.size()) ++selected;
                        refresh = true;
                    }
                }
            }
            if (pressed & HidNpadButton_Up) { if (!files.empty()) selected = (selected + files.size() - 1) % files.size(); refresh = true; }
            if (pressed & HidNpadButton_L) { shared = !shared; folder = "root"; parents.clear(); reload = true; refresh = true; }
            if (pressed & HidNpadButton_B) { if (parents.empty()) return; folder = parents.back(); parents.pop_back(); shared = false; reload = true; refresh = true; }
            if (files.empty()) { consoleUpdate(nullptr); continue; }
            auto& file = files[selected];
            if (pressed & HidNpadButton_A && file.folder) { parents.push_back(folder); folder = file.id; shared = false; reload = true; refresh = true; }
            if ((pressed & (HidNpadButton_X | HidNpadButton_Y)) && file.canDownload && !file.folder) {
                downloadFile(store, state, file, (pressed & HidNpadButton_Y) != 0);
                refresh = true;
            }
            consoleUpdate(nullptr);
        }
        state.lastFolderId = folder;
        saveOrShow(store, state);
    }
}

bool managedNsp(const LibraryItem& item) {
    return item.installed == InstallKind::Nsp && item.nspInstallState == NspInstallState::Installed && !item.nspMetaId.empty();
}

void deleteLibraryDownload(StateStore& store, State& state, const LibraryItem& item) {
    title(tr(TextId::DeleteDownload));
    printf("%s\n\n%s\n", item.name.c_str(), tr(TextId::DeleteDownloadWarning));
    hint(tr(TextId::RemoveConfirm));
    consoleUpdate(nullptr);
    while (appletMainLoop()) {
        hidScanInput();
        const auto confirmation = hidKeysDown(CONTROLLER_P1_AUTO);
        if (confirmation & HidNpadButton_B) return;
        if (confirmation & HidNpadButton_X) {
            std::string error;
            if (!store.removeDownload(state, item.id, error)) {
                printf("\n"); printf(tr(TextId::RemovalFailed), error.c_str()); printf("\n");
                waitForButton();
            }
            return;
        }
        consoleUpdate(nullptr);
    }
}

void library(StateStore& store, State& state) {
    size_t selected = 0;
    bool redraw = true;
    while (appletMainLoop()) {
        if (redraw) {
            selected = state.library.empty() ? 0 : std::min(selected, state.library.size() - 1);
            title(tr(TextId::Library));
            if (state.library.empty()) printf("%s\n", tr(TextId::NoIndexedDownloads));
            ui::instance().setSubtitle(tr(TextId::LibrarySubtitle));
            std::vector<ui::Row> rows;
            for (const auto& item : state.library) rows.push_back({item.name, item.nspInstallState == NspInstallState::Installed ? tr(TextId::ManagedNspInstalled) : item.localState == LocalState::Present ? fileSize(item.size) : tr(TextId::RemovedAfterInstall), ui::Icon::File});
            ui::instance().setRows(std::move(rows), selected);
            hint(tr(!state.library.empty() && managedNsp(state.library[selected]) && state.library[selected].localState == LocalState::Present ? TextId::LibraryInstalledHint : TextId::LibraryHint));
            consoleUpdate(nullptr);
            redraw = false;
        }
        hidScanInput();
        const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        const int touched = ui::instance().takeRowSelection();
        if (touched >= 0 && static_cast<size_t>(touched) < state.library.size()) { selected = static_cast<size_t>(touched); redraw = true; }

        if ((pressed & HidNpadButton_Down) && !state.library.empty()) { selected = (selected + 1) % state.library.size(); redraw = true; }
        if ((pressed & HidNpadButton_Up) && !state.library.empty()) { selected = (selected + state.library.size() - 1) % state.library.size(); redraw = true; }
        if ((pressed & HidNpadButton_A) && !state.library.empty()) {
            auto& item = state.library[selected];
            if (item.localState == LocalState::Present && !LocalFile::exists(item.localPath, item.storageKind)) {
                deleteLibraryDownload(store, state, item);
                redraw = true;
                continue;
            } else if (item.localState == LocalState::Present && isInstallablePackage(item.name)) {
                const auto task = std::find_if(state.tasks.begin(), state.tasks.end(), [&](const Task& candidate) { return candidate.id == item.id; });
                if (task == state.tasks.end()) {
                    printf("\n"); printf(tr(TextId::InstallFailed), tr(TextId::DownloadRecordMissing)); printf("\n");
                } else {
                    task->installAfterDownload = true;
                    installDownloaded(store, state, *task);
                }
                waitForButton();
            }
            return;
        }
        if ((pressed & (HidNpadButton_Y | HidNpadButton_X)) && !state.library.empty()) {
            auto& item = state.library[selected];
            if ((pressed & HidNpadButton_Y) && (item.localState == LocalState::Present || !managedNsp(item))) {
                deleteLibraryDownload(store, state, item);
                redraw = true;
                continue;
            }
            if (!managedNsp(item)) continue;
            title(tr(TextId::RemoveNsp));
            printf("%s\n%s %s\n", item.name.c_str(), nspContentKindName(item.nspContentKind), item.nspMetaId.c_str());
            if (item.nspContentKind == NspContentKind::BaseGame) printf("%s\n", tr(TextId::BaseRemovalWarning));
            hint(tr(TextId::RemoveConfirm));
            consoleUpdate(nullptr);
            while (appletMainLoop()) { hidScanInput(); const auto confirmation = hidKeysDown(CONTROLLER_P1_AUTO); if (confirmation & HidNpadButton_B) break; if (confirmation & HidNpadButton_X) {
                InstalledNspInfo target{true, item.nspStorage, item.nspVersion, item.nspMetaId, item.nspBaseTitleId, item.nspContentKind}; NspInstallJournal journal; std::string error;
                if (NspInstaller{}.uninstall(target, store, journal, error)) { item.installed = InstallKind::None; item.nspInstallState = NspInstallState::None; item.installedContentId.clear(); saveOrShow(store, state); }
                else { printf("\n"); printf(tr(TextId::RemovalFailed), error.c_str()); printf("\n"); waitForButton(); }
                break;
            } consoleUpdate(nullptr); }
            redraw = true;
            continue;
        }
        if (pressed & HidNpadButton_B) return;
        consoleUpdate(nullptr);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::string graphicsError;
    if (!ui::instance().initialize(graphicsError)) {
        ui::instance().diagnostic(graphicsError.c_str());
        ui::instance().enableConsoleFallback(graphicsError);
    }
    ui::instance().diagnostic("main: ui initialized or fallback active");
    SocketInitConfig socketConfig = *socketGetDefaultInitConfig();
    socketConfig.tcp_rx_buf_size = 256 * 1024;
    socketConfig.tcp_rx_buf_max_size = 512 * 1024;
    socketConfig.sb_efficiency = 4;
    Result socketResult = socketInitialize(&socketConfig);
    bool socketFallback{};
    if (R_FAILED(socketResult)) {
        socketFallback = true;
        socketConfig = *socketGetDefaultInitConfig();
        socketResult = socketInitialize(&socketConfig);
    }
    networkTcpRxSize = socketConfig.tcp_rx_buf_size;
    networkTcpRxMaxSize = socketConfig.tcp_rx_buf_max_size;
    networkSocketFallback = socketFallback;
    const CURLcode curlResult = curl_global_init(CURL_GLOBAL_DEFAULT);
    networkReady = R_SUCCEEDED(socketResult) && curlResult == CURLE_OK;
    networkResult = R_FAILED(socketResult) ? socketResult : static_cast<uint32_t>(curlResult);
    char networkDiagnostic[128]{};
    std::snprintf(networkDiagnostic, sizeof(networkDiagnostic), "main: socket=%08x curl=%d", socketResult, static_cast<int>(curlResult));
    ui::instance().diagnostic(networkDiagnostic);
    std::snprintf(networkDiagnostic, sizeof(networkDiagnostic), tr(TextId::NetworkProfileDiagnostic), ui::instance().appletMode() ? 1 : 0,
        socketConfig.tcp_rx_buf_size, socketConfig.tcp_rx_buf_max_size, socketResult, socketFallback ? 1 : 0);
    ui::instance().diagnostic(networkDiagnostic);
    StateStore store(kRoot);
    State state = store.load();
    ui::instance().diagnostic("main: state loaded");
    setLanguage(parseLanguage(state.language));
    ui::instance().setBrand(tr(TextId::AppName));
    if (ui::instance().appletMode()) ui::instance().setAppletWarning(tr(TextId::AppletModeWarning));
    if (!ui::instance().graphical()) {
        title(tr(TextId::AppName));
        printf(tr(TextId::GraphicsUnavailable), graphicsError.c_str()); printf("\n");
        waitForButton();
        curl_global_cleanup();
        if (R_SUCCEEDED(socketResult)) socketExit();
        ui::instance().shutdown();
        return 1;
    }
    if (state.serviceUrl.empty()) state.serviceUrl = configServiceUrl();
    ui::instance().diagnostic("main: configuration loaded");
    recoverInstallJournal(store, state);
    ui::instance().diagnostic("main: install journal recovered");
    recoverTasks(store, state);
    ui::instance().diagnostic("main: downloads recovered");

    int page = 0;
    ui::instance().diagnostic("main: entering application loop");
    while (appletMainLoop()) {
        ui::instance().setBrand(tr(TextId::AppName));
        if (ui::instance().appletMode()) ui::instance().setAppletWarning(tr(TextId::AppletModeWarning));
        mainTitle(page);
        static constexpr std::array<TextId, 4> subtitles{TextId::HomeSubtitle, TextId::FilesSubtitle, TextId::LibrarySubtitle, TextId::SettingsSubtitle};
        ui::instance().setSubtitle(tr(subtitles[static_cast<size_t>(page)]));
        if (page == 0) {
            ui::instance().setCards({
                {tr(TextId::ConnectDrive), accountName(state), HidNpadButton_A, ui::Icon::Cloud},
                {tr(TextId::Files), activeProviderName(state), HidNpadButton_X, ui::Icon::Folder},
                {tr(TextId::Library), tr(TextId::LibrarySubtitle), HidNpadButton_Y, ui::Icon::Library},
            });
        } else if (page == 1) {
            ui::instance().setCards({{tr(TextId::StorageProviders), tr(TextId::FilesSubtitle), HidNpadButton_A, ui::Icon::Folder}});
        } else if (page == 2) {
            char detail[96]{};
            std::snprintf(detail, sizeof(detail), tr(TextId::OpenLibrary), state.library.size());
            ui::instance().setCards({{tr(TextId::Library), detail, HidNpadButton_A, ui::Icon::Library}});
        } else {
            ui::instance().setCards({
                {tr(TextId::AutoCleanup), state.deleteAfterInstall ? tr(TextId::Yes) : tr(TextId::No), HidNpadButton_A, ui::Icon::Settings},
                {tr(TextId::ConnectDrive), accountName(state), HidNpadButton_X, ui::Icon::Cloud},
                {tr(TextId::Language), std::string(languageName(currentLanguage())), HidNpadButton_Y, ui::Icon::Language},
                {tr(TextId::HomeStorage), tr(TextId::StorageProviders), HidNpadButton_ZL, ui::Icon::Cloud},
            });
        }
        mainHint(tr(TextId::NavigationHint));
        hidScanInput();
        const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        if (pressed & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
        const int touchedTab = ui::instance().takeTabSelection();
        const uint64_t action = ui::instance().takeCardAction();
        if (touchedTab >= 0) { page = touchedTab; continue; }
        // The UI resolves A to the selected card; X/Y/ZL remain direct shortcuts.
        // Dispatch exactly one action, then rebuild the main screen after any
        // nested dialog so stale dialog state cannot receive the next input.
        if (page == 0 && action == HidNpadButton_A) connectAccount(store, state);
        else if (page == 0 && action == HidNpadButton_Y) library(store, state);
        else if (page == 0 && action == HidNpadButton_X) chooseStorageProvider(store, state);
        else if (page == 1 && action == HidNpadButton_A) chooseStorageProvider(store, state);
        else if (page == 2 && action == HidNpadButton_A) library(store, state);
        else if (page == 3 && action == HidNpadButton_A) { state.deleteAfterInstall = !state.deleteAfterInstall; saveOrShow(store, state); }
        else if (page == 3 && action == HidNpadButton_X) connectAccount(store, state);
        else if (page == 3 && action == HidNpadButton_Y) { setLanguage(nextLanguage(currentLanguage())); state.language = languageCode(currentLanguage()); saveOrShow(store, state); }
        else if (page == 3 && action == HidNpadButton_ZL) configureHomeStorage(store, state);
    }
    curl_global_cleanup();
    if (R_SUCCEEDED(socketResult)) socketExit();
    ui::instance().shutdown();
    return 0;
}
