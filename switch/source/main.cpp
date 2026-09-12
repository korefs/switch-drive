#include "switchdrive/core.hpp"
#include "switchdrive/i18n.hpp"
#include "switchdrive/network.hpp"

#include <switch.h>
#include <mbedtls/md5.h>
#include <curl/curl.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;
using namespace switchdrive;
using namespace switchdrive::i18n;

namespace {

PadState gPad;
#define hidScanInput() padUpdate(&gPad)
#define hidKeysDown(_unused) padGetButtonsDown(&gPad)
#define CONTROLLER_P1_AUTO 0

constexpr const char* kRoot = "sdmc:/switch-drive";
constexpr const char* kDefaultService = "";
constexpr uint64_t kCheckpointBytes = 64ULL * 1024ULL * 1024ULL;
constexpr auto kCheckpointInterval = std::chrono::seconds(10);

void title(const char* page) {
    consoleClear();
    printf("\x1b[36;1m%s\x1b[0m  |  %s\n", tr(TextId::AppName), page);
    printf("────────────────────────────────────────────────────────\n");
}

void hint(const char* text) { printf("\n\x1b[90m%s\x1b[0m\n", text); }

void waitForButton() {
    printf("\n%s", tr(TextId::Continue));
    while (appletMainLoop()) {
        hidScanInput();
        if (hidKeysDown(CONTROLLER_P1_AUTO) & HidNpadButton_A) return;
        consoleUpdate(nullptr);
    }
}

bool chooseNspDestination(const NspPackageInfo& package, const std::vector<InstalledNspInfo>& installed, NspInstallStorage& destination) {
    destination = NspInstallStorage::SdCard;
    while (appletMainLoop()) {
        title(tr(TextId::InstallNsp));
        printf("%s\n", nspContentKindName(package.kind));
        printf(tr(TextId::TitleId), package.baseTitleId.c_str()); printf("\n");
        printf(tr(TextId::Version), package.version); printf("\n");
        if (!installed.empty()) { printf(tr(TextId::InstalledVersion), installed.front().version, nspInstallStorageName(installed.front().storage)); printf("\n"); }
        printf("\n%s %s\n%s %s\n", destination == NspInstallStorage::SdCard ? ">" : " ", tr(TextId::SdCard), destination == NspInstallStorage::InternalUser ? ">" : " ", tr(TextId::InternalStorage));
        hint(tr(TextId::DestinationHint));
        hidScanInput(); const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO);
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
    }
    std::array<unsigned char, 16> raw{};
    mbedtls_md5_finish(&context, raw.data());
    mbedtls_md5_free(&context);
    char output[33]{};
    for (size_t i = 0; i < raw.size(); ++i) std::snprintf(output + i * 2, 3, "%02x", raw[i]);
    digest = output;
    return true;
}

bool connectAccount(StateStore& store, State& state) {
    title(tr(TextId::ConnectDrive));
    if (state.serviceUrl.empty()) {
        printf("%s\n", tr(TextId::ConfigMissing));
        waitForButton();
        return false;
    }
    if (state.consolePublicKey.empty()) state.consolePublicKey = makeId() + makeId();
    AuthClient auth(HttpClient{}, state.serviceUrl);
    std::string id, url, code, pollSecret, error;
    if (!auth.begin(state.consolePublicKey, id, url, code, pollSecret, error)) {
        printf(tr(TextId::StartFailed), error.c_str()); printf("\n");
        waitForButton();
        return false;
    }
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
    if (state.lastAccountId.empty()) {
        error = tr(TextId::ConnectAccountFirst);
        return false;
    }
    return AuthClient(HttpClient{}, state.serviceUrl).accessToken(state.sessionToken, state.lastAccountId, token, error);
}

bool sameRemote(const Task& task, const RemoteFile& remote, const std::string& accountId) {
    return task.accountId == accountId && task.remoteId == remote.id && task.revision == remote.revision && task.expectedSize == remote.size && task.md5 == remote.md5;
}

bool hasResumeIdentity(const Task& task) {
    return !task.accountId.empty() && !task.remoteId.empty() && !task.revision.empty() && task.expectedSize > 0 && !task.localPath.empty();
}

Task* findIncompleteTask(State& state, const std::string& accountId, const std::string& remoteId) {
    for (auto& task : state.tasks) {
        if (task.accountId == accountId && task.remoteId == remoteId && task.state != TaskState::Completed && task.state != TaskState::Cancelled) return &task;
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

bool checkpointTask(StateStore& store, State& state, Task& task, LocalFile& file, std::string& error) {
    uint64_t size{};
    if (!file.flush(error) || !file.size(size, error)) return false;
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

void applyRemote(Task& task, const RemoteFile& remote, const std::string& accountId, const StateStore& store) {
    task.accountId = accountId;
    task.remoteId = remote.id;
    task.displayName = remote.name;
    task.expectedSize = remote.size;
    task.md5 = remote.md5;
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
    if (!md5File(task.localPath, task.storageKind, digest, error)) return false;
    if (!task.md5.empty() && digest != task.md5) {
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
        item.accountId = task.accountId;
        item.remoteId = task.remoteId;
        item.name = task.displayName;
        item.localPath = task.localPath;
        item.md5 = task.md5;
        item.size = task.expectedSize;
        item.localState = LocalState::Present;
        item.storageKind = task.storageKind;
        state.library.push_back(std::move(item));
    }
    return store.save(state, error);
}

void installIfRequested(StateStore& store, State& state, Task& task) {
    if (!task.installAfterDownload || state.library.empty()) return;
    auto library = std::find_if(state.library.begin(), state.library.end(), [&](const LibraryItem& item) { return item.id == task.id; });
    if (library == state.library.end()) return;
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
    } else if (isNsp(task.displayName)) {
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
                if (!chooseNspDestination(package, existing, destination)) return;
                NspInstallJournal journal; journal.libraryId = library->id; journal.localPath = task.localPath; journal.deletePackage = task.deleteAfterInstall;
                library->nspInstallState = NspInstallState::Installing; saveOrShow(store, state);
                installed = installer.install(task.localPath, task.storageKind, package, destination, store, journal, [](uint64_t current, uint64_t total) { printf("\r"); printf(tr(TextId::InstallingBytes), static_cast<unsigned long long>(current), static_cast<unsigned long long>(total)); consoleUpdate(nullptr); return appletMainLoop(); }, error);
                if (installed) {
                    library->installed = InstallKind::Nsp; library->installedContentId = package.metaId; library->nspContentKind = package.kind; library->nspStorage = destination; library->nspMetaId = package.metaId; library->nspBaseTitleId = package.baseTitleId; library->nspVersion = package.version; library->nspInstallState = NspInstallState::Installed;
                } else library->nspInstallState = NspInstallState::Failed;
            }
        }
        if (installed) {
            library->nspInstallState = NspInstallState::Installed;
        }
    } else {
        return;
    }
    if (!installed) {
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
}

void downloadFile(StateStore& store, State& state, const RemoteFile& remote, bool installAfter) {
    std::string token, error;
    if (!acquireToken(state, token, error)) {
        printf("\n%s", error.c_str());
        waitForButton();
        return;
    }

    Task* task = findIncompleteTask(state, state.lastAccountId, remote.id);
    bool restart = false;
    if (task) {
        const bool changed = !sameRemote(*task, remote, state.lastAccountId);
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
        applyRemote(newTask, remote, state.lastAccountId, store);
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
        applyRemote(*task, remote, state.lastAccountId, store);
    }
    task->installAfterDownload = task->installAfterDownload || installAfter;
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
    printf(tr(TextId::Downloading), task->displayName.c_str()); printf("\n");
    auto lastCheckpoint = task->committedBytes;
    auto lastCheckpointAt = std::chrono::steady_clock::now();
    DownloadResult result;
    const bool downloaded = task->committedBytes == task->expectedSize || HttpClient{}.download(
        DriveClient(HttpClient{}).mediaUrl(remote),
        {"Authorization: Bearer " + token},
        output,
        task->committedBytes,
        task->expectedSize,
        task->etag,
        [&](const std::string& etag) {
            task->etag = etag;
            return store.save(state, error);
        },
        [&](uint64_t received) {
            printf("\r"); printf(tr(TextId::BytesProgress), static_cast<unsigned long long>(received), static_cast<unsigned long long>(task->expectedSize)); printf("   ");
            const auto now = std::chrono::steady_clock::now();
            if (received - lastCheckpoint >= kCheckpointBytes && now - lastCheckpointAt >= kCheckpointInterval) {
                if (!checkpointTask(store, state, *task, output, error)) return false;
                lastCheckpoint = task->committedBytes;
                lastCheckpointAt = now;
            }
            consoleUpdate(nullptr);
            return appletMainLoop();
        },
        result,
        error);

    std::string checkpointError;
    if (!checkpointTask(store, state, *task, output, checkpointError) && error.empty()) error = checkpointError;
    output.close();
    if (!downloaded) {
        task->state = result.status == DownloadStatus::RangeRejected ? TaskState::Failed : TaskState::Paused;
        task->error = error;
        saveOrShow(store, state);
        printf("\n%s: %s\n", tr(result.status == DownloadStatus::RangeRejected ? TextId::RangeRejected : TextId::Paused), error.c_str());
        waitForButton();
        return;
    }

    if (!verifyAndRecord(store, state, *task, error)) {
        task->state = TaskState::Failed;
        task->error = error;
        saveOrShow(store, state);
        printf("\n"); printf(tr(TextId::DownloadInvalid), error.c_str()); printf("\n");
        waitForButton();
        return;
    }
    printf("\n%s\n", tr(TextId::DownloadComplete));
    installIfRequested(store, state, *task);
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

void browse(StateStore& store, State& state) {
    std::string token, error;
    if (!acquireToken(state, token, error)) {
        title(tr(TextId::Files));
        printf("%s\n", error.c_str());
        waitForButton();
        return;
    }
    DriveClient drive(HttpClient{});
    std::string folder = state.lastFolderId.empty() ? "root" : state.lastFolderId;
    std::vector<std::string> parents;
    bool shared = false;
    size_t selected = 0;
    while (appletMainLoop()) {
        std::vector<RemoteFile> files;
        std::string next;
        if (!drive.list(token, folder, shared, "", files, next, error)) {
            title(tr(TextId::Files));
            printf(tr(TextId::DriveError), error.c_str()); printf("\n");
            waitForButton();
            return;
        }
        title(tr(shared ? TextId::SharedWithMe : TextId::MyDrive));
        printf(tr(TextId::AccountLabel), state.lastAccountId.c_str()); printf("\n\n");
        if (files.empty()) printf("%s\n", tr(TextId::EmptyFolder));
        for (size_t i = 0; i < files.size() && i < 20; ++i) {
            const auto& file = files[i];
            printf("%s %c %-42s %10llu\n", i == selected ? ">" : " ", file.folder ? 'D' : 'F', file.name.c_str(), static_cast<unsigned long long>(file.size));
        }
        hint(tr(TextId::BrowseHint));
        bool refresh = false;
        while (appletMainLoop() && !refresh) {
            hidScanInput();
            const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO);
            if (pressed & HidNpadButton_Down) { if (!files.empty()) selected = (selected + 1) % files.size(); refresh = true; }
            if (pressed & HidNpadButton_Up) { if (!files.empty()) selected = (selected + files.size() - 1) % files.size(); refresh = true; }
            if (pressed & HidNpadButton_L) { shared = !shared; folder = "root"; parents.clear(); selected = 0; refresh = true; }
            if (pressed & HidNpadButton_B) { if (parents.empty()) return; folder = parents.back(); parents.pop_back(); shared = false; selected = 0; refresh = true; }
            if (files.empty()) continue;
            auto& file = files[selected];
            if (pressed & HidNpadButton_A && file.folder) { parents.push_back(folder); folder = file.id; shared = false; selected = 0; refresh = true; }
            if ((pressed & HidNpadButton_X) && file.canDownload && !file.folder) { downloadFile(store, state, file, false); refresh = true; }
            if ((pressed & HidNpadButton_Y) && file.canDownload && !file.folder) { downloadFile(store, state, file, true); refresh = true; }
            consoleUpdate(nullptr);
        }
        state.lastFolderId = folder;
        saveOrShow(store, state);
    }
}

void library(StateStore& store, State& state) {
    title(tr(TextId::Library));
    if (state.library.empty()) printf("%s\n", tr(TextId::NoIndexedDownloads));
    size_t selected = 0;
    for (size_t i = 0; i < state.library.size(); ++i) {
        const auto& item = state.library[i];
        printf("%c %zu. %s  [%s%s]\n", i == selected ? '>' : ' ', i + 1, item.name.c_str(), item.localState == LocalState::Present ? tr(TextId::LocalFile) : tr(TextId::RemovedAfterInstall), item.nspInstallState == NspInstallState::Installed ? tr(TextId::NspInstalledSuffix) : "");
    }
    hint(tr(TextId::LibraryHint));
    while (appletMainLoop()) {
        hidScanInput();
        const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        if ((pressed & HidNpadButton_Down) && !state.library.empty()) { selected = (selected + 1) % state.library.size(); return library(store, state); }
        if ((pressed & HidNpadButton_Up) && !state.library.empty()) { selected = (selected + state.library.size() - 1) % state.library.size(); return library(store, state); }
        if ((pressed & HidNpadButton_A) && !state.library.empty()) {
            auto& item = state.library[selected];
            if (item.localState == LocalState::Present && !LocalFile::exists(item.localPath, item.storageKind)) {
                printf("\n%s\n", tr(TextId::RemoveShortcutQuestion));
                while (appletMainLoop()) {
                    hidScanInput();
                    const auto confirmation = hidKeysDown(CONTROLLER_P1_AUTO);
                    if (confirmation & HidNpadButton_X) { state.library.erase(state.library.begin() + selected); saveOrShow(store, state); return; }
                    if (confirmation & HidNpadButton_B) break;
                    consoleUpdate(nullptr);
                }
            }
            return;
        }
        if ((pressed & HidNpadButton_Y) && !state.library.empty()) {
            auto& item = state.library[selected];
            if (item.installed != InstallKind::Nsp || item.nspInstallState != NspInstallState::Installed || item.nspMetaId.empty()) { printf("\n%s\n", tr(TextId::NoManagedNsp)); waitForButton(); return; }
            title(tr(TextId::RemoveNsp));
            printf("%s\n%s %s\n", item.name.c_str(), nspContentKindName(item.nspContentKind), item.nspMetaId.c_str());
            if (item.nspContentKind == NspContentKind::BaseGame) printf("%s\n", tr(TextId::BaseRemovalWarning));
            hint(tr(TextId::RemoveConfirm));
            while (appletMainLoop()) { hidScanInput(); const auto confirmation = hidKeysDown(CONTROLLER_P1_AUTO); if (confirmation & HidNpadButton_B) return; if (confirmation & HidNpadButton_X) {
                InstalledNspInfo target{true, item.nspStorage, item.nspVersion, item.nspMetaId, item.nspBaseTitleId, item.nspContentKind}; NspInstallJournal journal; std::string error;
                if (NspInstaller{}.uninstall(target, store, journal, error)) { item.installed = InstallKind::None; item.nspInstallState = NspInstallState::None; item.installedContentId.clear(); saveOrShow(store, state); }
                else { printf("\n"); printf(tr(TextId::RemovalFailed), error.c_str()); printf("\n"); waitForButton(); }
                return;
            } consoleUpdate(nullptr); }
        }
        if (pressed & HidNpadButton_B) return;
        consoleUpdate(nullptr);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&gPad);
    socketInitializeDefault();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    StateStore store(kRoot);
    State state = store.load();
    setLanguage(parseLanguage(state.language));
    if (state.serviceUrl.empty()) state.serviceUrl = configServiceUrl();
    recoverInstallJournal(store, state);
    recoverTasks(store, state);

    int page = 0;
    while (appletMainLoop()) {
        title(tr(page == 0 ? TextId::Home : page == 1 ? TextId::Files : page == 2 ? TextId::Library : TextId::Settings));
        if (page == 0) {
            if (state.accounts.empty()) printf("%s\n", tr(TextId::NoAccountConnected));
            else { printf(tr(TextId::ActiveAccount), state.lastAccountId.c_str()); printf("\n"); }
            printf("\n%s\n", tr(TextId::OpenFiles));
        } else if (page == 1) {
            printf("%s\n", tr(TextId::BrowseDrive));
        } else if (page == 2) {
            printf(tr(TextId::OpenLibrary), state.library.size()); printf("\n");
        } else {
            printf(tr(TextId::CleanupAfterInstall), state.deleteAfterInstall ? tr(TextId::Yes) : tr(TextId::No)); printf("\n");
            printf("%s: %s\n", tr(TextId::Language), languageName(currentLanguage()).data());
            printf("%s\n%s\n", tr(TextId::ToggleCleanup), tr(TextId::ChangeLanguage));
        }
        hint(tr(TextId::NavigationHint));
        hidScanInput();
        const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        if (pressed & HidNpadButton_Plus) break;
        if (pressed & HidNpadButton_R) page = (page + 1) % 4;
        if (pressed & HidNpadButton_L) page = (page + 3) % 4;
        if (page == 0 && (pressed & HidNpadButton_A)) connectAccount(store, state);
        if (page == 0 && (pressed & HidNpadButton_X)) browse(store, state);
        if (page == 1 && (pressed & HidNpadButton_A)) browse(store, state);
        if (page == 2 && (pressed & HidNpadButton_A)) library(store, state);
        if (page == 3 && (pressed & HidNpadButton_A)) { state.deleteAfterInstall = !state.deleteAfterInstall; saveOrShow(store, state); }
        if (page == 3 && (pressed & HidNpadButton_X)) connectAccount(store, state);
        if (page == 3 && (pressed & HidNpadButton_Y)) { setLanguage(nextLanguage(currentLanguage())); state.language = languageCode(currentLanguage()); saveOrShow(store, state); }
        consoleUpdate(nullptr);
    }
    curl_global_cleanup();
    socketExit();
    consoleExit(nullptr);
    return 0;
}
