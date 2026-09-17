#include "switchdrive/app_controller.hpp"

#include <mbedtls/md5.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <map>
#include <thread>

namespace fs = std::filesystem;

namespace switchdrive {
namespace {

constexpr uint64_t kCheckpointBytes = 64ULL * 1024ULL * 1024ULL;
constexpr auto kCheckpointInterval = std::chrono::seconds(10);
constexpr auto kTransferUiInterval = std::chrono::milliseconds(100);

std::string formatted(i18n::TextId id, const std::string& value) {
    char text[768]{};
    std::snprintf(text, sizeof(text), i18n::tr(id), value.c_str());
    return text;
}

std::string networkError(uint32_t result) {
    char text[256]{};
    std::snprintf(text, sizeof(text), i18n::tr(i18n::TextId::NetworkUnavailable), result);
    return text;
}

std::string dataSize(uint64_t bytes) {
    char text[64]{};
    std::snprintf(text, sizeof(text), i18n::tr(i18n::TextId::FileSize),
        static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

bool sameRemote(const Task& task, const RemoteEntry& remote, const std::string& accountId) {
    const std::string providerId = remote.providerId.empty() ? "google-drive" : remote.providerId;
    const bool checksumMatches = remote.checksum.kind == ChecksumKind::Sha256
        ? task.sha256 == remote.checksum.value
        : remote.checksum.kind == ChecksumKind::Md5
            ? task.md5 == remote.checksum.value
            : task.md5.empty() && task.sha256.empty();
    return task.providerId == providerId && task.accountId == accountId &&
        task.remoteId == remote.id && task.revision == remote.revision &&
        task.expectedSize == remote.size && checksumMatches;
}

bool hasResumeIdentity(const Task& task) {
    return !task.providerId.empty() && !task.remoteId.empty() && !task.revision.empty() &&
        task.expectedSize > 0 && !task.localPath.empty();
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
        error = i18n::tr(i18n::TextId::PartialTooLarge);
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

bool hasEnoughSpace(uint64_t needed) {
    std::error_code error;
    const auto space = fs::space("sdmc:/", error);
    return !error && space.available >= needed;
}

ui::FileRowModel fileRow(const RemoteEntry& entry, const State& state) {
    const std::string providerId = entry.providerId.empty() ? "google-drive" : entry.providerId;
    const auto provider = std::find_if(state.providers.begin(), state.providers.end(),
        [&](const ProviderConfig& value) { return value.id == providerId; });
    const std::string accountId = provider != state.providers.end() && provider->kind == ProviderKind::GoogleDrive
        ? state.lastAccountId : "";
    const auto task = std::find_if(state.tasks.begin(), state.tasks.end(), [&](const Task& value) {
        return value.providerId == providerId && value.accountId == accountId && value.remoteId == entry.id &&
            value.state != TaskState::Completed && value.state != TaskState::Cancelled;
    });
    const bool partial = task != state.tasks.end() &&
        (task->committedBytes > 0 || !sameRemote(*task, entry, accountId) || !hasResumeIdentity(*task));
    const bool restartRequired = task != state.tasks.end() &&
        (!sameRemote(*task, entry, accountId) || !hasResumeIdentity(*task));
    return {entry.id, entry.name,
        entry.folder ? i18n::tr(i18n::TextId::Folder) : dataSize(entry.size),
        entry.folder, !entry.folder && (isInstallablePackage(entry.name) || isNro(entry.name)), partial, restartRequired,
        entry.canDownload && !entry.folder, entry.canHide};
}

} // namespace

struct AppController::Impl {
    fs::path root;
    StateStore store;
    mutable std::mutex stateMutex;
    State state;
    bool applet{};
    bool networkReady{};
    uint32_t networkResult{};

    mutable std::mutex observerMutex;
    std::map<size_t, Observer> observers;
    size_t nextObserver{1};

    mutable std::mutex filesMutex;
    ui::FilesModel files;
    std::vector<RemoteEntry> remoteFiles;
    std::string folder{"root"};
    std::vector<std::string> parentFolders;
    std::vector<std::string> parentNames;

    mutable std::mutex pairingMutex;
    PairingModel pairing;
    std::string pairingId;
    std::string pairingSecret;

    mutable std::mutex homeMutex;
    HomeStorageSetupModel homeSetup;

    ui::OperationGate operation;
    std::mutex workerMutex;
    std::thread worker;
    std::atomic<bool> stopping{};

    Impl(fs::path root, bool applet)
        : root(std::move(root)), store(this->root), state(store.load()), applet(applet) {
        i18n::setLanguage(i18n::parseLanguage(state.language));
        if (state.serviceUrl.empty()) {
            std::ifstream input(this->root / "config.json");
            const std::string config((std::istreambuf_iterator<char>(input)), {});
            const std::string marker = "\"service_url\":\"";
            const auto start = config.find(marker);
            if (start != std::string::npos) {
                const auto begin = start + marker.size();
                const auto end = config.find('"', begin);
                if (end != std::string::npos) state.serviceUrl = config.substr(begin, end - begin);
            }
        }
        const auto provider = std::find_if(state.providers.begin(), state.providers.end(), [&](const ProviderConfig& value) {
            return value.id == state.activeProviderId;
        });
        if (provider != state.providers.end()) folder = provider->lastFolderId.empty() ? "root" : provider->lastFolderId;
        setFilesHeader();
    }

    ~Impl() {
        stop();
    }

    void stop() {
        stopping = true;
        operation.requestCancel();
        joinWorker();
    }

    void joinWorker() {
        std::thread previous;
        {
            std::scoped_lock lock(workerMutex);
            if (worker.joinable()) previous = std::move(worker);
        }
        if (previous.joinable()) previous.join();
    }

    void notify() {
        std::vector<Observer> callbacks;
        {
            std::scoped_lock lock(observerMutex);
            callbacks.reserve(observers.size());
            for (const auto& [id, callback] : observers) {
                (void)id;
                callbacks.push_back(callback);
            }
        }
        for (const auto& callback : callbacks) if (callback) callback();
    }

    template <typename Work>
    bool launch(ui::OperationPhase phase, i18n::TextId title, i18n::TextId message, bool cancellable, Work work) {
        if (stopping) return false;
        const auto generation = operation.start(phase, i18n::tr(title), i18n::tr(message), cancellable);
        if (!generation) return false;
        notify();
        joinWorker();
        std::scoped_lock lock(workerMutex);
        if (stopping) {
            operation.finish(generation, ui::OperationPhase::Cancelled,
                i18n::tr(i18n::TextId::OperationCancelled));
            return false;
        }
        worker = std::thread([this, generation, work = std::move(work)]() mutable {
            try {
                work(generation);
            } catch (const std::exception& exception) {
                operation.finish(generation, ui::OperationPhase::Failed, exception.what());
            }
            notify();
        });
        return true;
    }

    HttpClient http(uint64_t generation) {
        return HttpClient{[this, generation] {
            return !stopping && operation.shouldContinue(generation);
        }};
    }

    bool saveLocked(std::string& error) { return store.save(state, error); }

    bool save(std::string& error) {
        std::scoped_lock lock(stateMutex);
        return saveLocked(error);
    }

    ProviderConfig providerCopy(const std::string& id) const {
        std::scoped_lock lock(stateMutex);
        for (const auto& provider : state.providers) if (provider.id == id) return provider;
        return {};
    }

    void persistFolder() {
        bool shared{};
        { std::scoped_lock lock(filesMutex); shared = files.shared; }
        std::string error;
        std::scoped_lock lock(stateMutex);
        const auto provider = std::find_if(state.providers.begin(), state.providers.end(), [&](const ProviderConfig& value) {
            return value.id == state.activeProviderId;
        });
        if (provider == state.providers.end() || (provider->kind == ProviderKind::GoogleDrive && shared)) return;
        provider->lastFolderId = folder.empty() ? "root" : folder;
        saveLocked(error);
    }

    void setFilesHeader() {
        std::scoped_lock filesLock(filesMutex);
        std::scoped_lock stateLock(stateMutex);
        const auto provider = std::find_if(state.providers.begin(), state.providers.end(), [&](const ProviderConfig& value) {
            return value.id == state.activeProviderId;
        });
        files.providerId = state.activeProviderId;
        files.providerName = provider == state.providers.end() || provider->kind == ProviderKind::GoogleDrive
            ? i18n::tr(i18n::TextId::MyDrive) : provider->name;
        files.canChangeScope = provider == state.providers.end() || provider->kind == ProviderKind::GoogleDrive;
        files.location = parentNames.empty() ? files.providerName : parentNames.back();
        files.breadcrumb = parentNames;
    }

    State stateCopy() const {
        std::scoped_lock lock(stateMutex);
        return state;
    }

    void upsertTask(const Task& task, std::string& error) {
        std::scoped_lock lock(stateMutex);
        const auto found = std::find_if(state.tasks.begin(), state.tasks.end(), [&](const Task& value) { return value.id == task.id; });
        if (found == state.tasks.end()) state.tasks.push_back(task); else *found = task;
        saveLocked(error);
    }

    void upsertTaskAndLibrary(const Task& task, const LibraryItem& item, std::string& error) {
        std::scoped_lock lock(stateMutex);
        const auto taskFound = std::find_if(state.tasks.begin(), state.tasks.end(), [&](const Task& value) { return value.id == task.id; });
        if (taskFound == state.tasks.end()) state.tasks.push_back(task); else *taskFound = task;
        const auto itemFound = std::find_if(state.library.begin(), state.library.end(), [&](const LibraryItem& value) { return value.id == item.id; });
        if (itemFound == state.library.end()) state.library.push_back(item); else *itemFound = item;
        saveLocked(error);
    }

    bool installDownloaded(Task& task, LibraryItem& item, NspInstallStorage destination,
        uint64_t generation, std::string& error) {
        operation.setPhase(generation, ui::OperationPhase::Installing,
            i18n::tr(i18n::TextId::InstallNsp), false);
        notify();
        task.state = TaskState::Installing;
        error.clear();
        upsertTaskAndLibrary(task, item, error);
        if (!error.empty()) {
            task.state = TaskState::Failed;
            task.error = error;
            return false;
        }
        bool installed{};
        bool journalCreated{};
        if (isNro(task.displayName)) {
            const auto base = sanitizeFileName(task.displayName.substr(0, task.displayName.size() - 4));
            const fs::path location = std::string("sdmc:/switch/") + base + "/" + sanitizeFileName(task.displayName);
            installed = NroInstaller{}.install(task.localPath, location, false, error);
        } else if (isInstallablePackage(task.displayName)) {
            NspInstaller installer;
            NspPackageInfo package;
            std::vector<InstalledNspInfo> existing;
            installed = installer.inspect(task.localPath, task.storageKind, package, error) &&
                installer.queryInstalled(package, existing, error);
            const auto decision = installed ? decideNspInstall(package, existing) : NspInstallDecision::Install;
            if (installed && decision == NspInstallDecision::DowngradeBlocked) {
                installed = false;
                error = i18n::tr(i18n::TextId::DowngradeBlocked);
            }
            if (installed && decision != NspInstallDecision::AlreadyInstalled) {
                NspInstallJournal journal;
                journal.libraryId = item.id;
                journal.localPath = task.localPath;
                journalCreated = true;
                installed = installer.install(task.localPath, task.storageKind, package, destination, store, journal,
                    [this, generation](uint64_t current, uint64_t total) {
                        operation.update(generation, current, total);
                        notify();
                        return operation.shouldContinue(generation);
                    }, error);
            }
        } else {
            error = i18n::tr(i18n::TextId::UnsupportedInstallType);
        }
        task.state = installed ? TaskState::Completed : TaskState::Failed;
        task.error = installed ? "" : error;
        if (!installed) {
            upsertTaskAndLibrary(task, item, error);
            return false;
        }

        // Record the completed installation before resolving its journal or
        // deleting the only local copy of the package.
        error.clear();
        upsertTaskAndLibrary(task, item, error);
        std::string cleanupError = error;
        if (cleanupError.empty() && journalCreated) store.clearInstallJournal(cleanupError);
        if (cleanupError.empty()) {
            std::scoped_lock lock(stateMutex);
            store.removeDownload(state, item.id, cleanupError);
        }
        if (!cleanupError.empty()) {
            error = formatted(i18n::TextId::InstalledCleanupPending, cleanupError);
            task.error = error;
            std::string saveError;
            upsertTaskAndLibrary(task, item, saveError);
        } else {
            error.clear();
        }
        return true;
    }

    bool digestFile(const Task& task, uint64_t generation, std::string& digest, std::string& error) {
        LocalFile file;
        uint64_t size{};
        if (!file.open(task.localPath, task.storageKind, false, error) || !file.size(size, error)) return false;
        std::array<unsigned char, 64 * 1024> buffer{};
        uint64_t offset{};
        if (!task.sha256.empty()) {
            mbedtls_sha256_context context;
            mbedtls_sha256_init(&context);
            mbedtls_sha256_starts(&context, 0);
            while (offset < size && operation.shouldContinue(generation)) {
                const auto chunk = static_cast<size_t>(std::min<uint64_t>(buffer.size(), size - offset));
                if (!file.readAt(offset, buffer.data(), chunk, error)) { mbedtls_sha256_free(&context); return false; }
                mbedtls_sha256_update(&context, buffer.data(), chunk);
                offset += chunk;
                operation.update(generation, offset, size);
                notify();
            }
            if (!operation.shouldContinue(generation)) { mbedtls_sha256_free(&context); error = i18n::tr(i18n::TextId::OperationCancelled); return false; }
            std::array<unsigned char, 32> raw{};
            mbedtls_sha256_finish(&context, raw.data());
            mbedtls_sha256_free(&context);
            char output[65]{};
            for (size_t index = 0; index < raw.size(); ++index) std::snprintf(output + index * 2, 3, "%02x", raw[index]);
            digest = output;
            return true;
        }
        mbedtls_md5_context context;
        mbedtls_md5_init(&context);
        mbedtls_md5_starts(&context);
        while (offset < size && operation.shouldContinue(generation)) {
            const auto chunk = static_cast<size_t>(std::min<uint64_t>(buffer.size(), size - offset));
            if (!file.readAt(offset, buffer.data(), chunk, error)) { mbedtls_md5_free(&context); return false; }
            mbedtls_md5_update(&context, buffer.data(), chunk);
            offset += chunk;
            operation.update(generation, offset, size);
            notify();
        }
        if (!operation.shouldContinue(generation)) { mbedtls_md5_free(&context); error = i18n::tr(i18n::TextId::OperationCancelled); return false; }
        std::array<unsigned char, 16> raw{};
        mbedtls_md5_finish(&context, raw.data());
        mbedtls_md5_free(&context);
        char output[33]{};
        for (size_t index = 0; index < raw.size(); ++index) std::snprintf(output + index * 2, 3, "%02x", raw[index]);
        digest = output;
        return true;
    }

    void finish(uint64_t generation, ui::OperationPhase phase, const std::string& message) {
        operation.finish(generation, phase, message);
    }
};

AppController::AppController(fs::path root, bool appletMode)
    : impl_(std::make_unique<Impl>(std::move(root), appletMode)) {}

AppController::~AppController() {
    // Worker lambdas capture the controller, so they must finish while the
    // controller (and its impl_ member) is still alive.
    if (impl_) impl_->stop();
}

void AppController::setNetworkStatus(bool ready, uint32_t result) {
    impl_->networkReady = ready;
    impl_->networkResult = result;
    impl_->notify();
}

void AppController::recover() {
    std::string error;
    std::string failedRecoveryTask;
    NspInstallJournal journal;
    bool exists{};
    if (impl_->store.loadInstallJournal(journal, error, exists) && exists) {
        const auto recovered = journal;
        bool installCommitted{};
        if (NspInstaller{}.recover(impl_->store, journal, installCommitted, error)) {
            std::scoped_lock lock(impl_->stateMutex);
            const auto item = std::find_if(impl_->state.library.begin(), impl_->state.library.end(), [&](const LibraryItem& value) {
                return value.id == recovered.libraryId;
            });
            if (item != impl_->state.library.end() && recovered.operation == "install") {
                if (installCommitted) {
                    const auto task = std::find_if(impl_->state.tasks.begin(), impl_->state.tasks.end(), [&](const Task& value) {
                        return value.id == recovered.libraryId;
                    });
                    if (task != impl_->state.tasks.end()) {
                        task->state = TaskState::Completed;
                        task->error.clear();
                    }
                    impl_->saveLocked(error);
                    std::string cleanupError;
                    if (!impl_->store.removeDownload(impl_->state, recovered.libraryId, cleanupError)) {
                        const auto retainedTask = std::find_if(impl_->state.tasks.begin(), impl_->state.tasks.end(), [&](const Task& value) {
                            return value.id == recovered.libraryId;
                        });
                        if (retainedTask != impl_->state.tasks.end())
                            retainedTask->error = formatted(i18n::TextId::InstalledCleanupPending, cleanupError);
                        impl_->saveLocked(error);
                    }
                } else {
                    const auto task = std::find_if(impl_->state.tasks.begin(), impl_->state.tasks.end(), [&](const Task& value) {
                        return value.id == recovered.libraryId;
                    });
                    if (task != impl_->state.tasks.end()) {
                        task->state = TaskState::Failed;
                        task->error = i18n::tr(i18n::TextId::InstallFailureUnknown);
                        failedRecoveryTask = task->id;
                    }
                    impl_->saveLocked(error);
                }
            }
        }
    }
    {
        std::scoped_lock lock(impl_->stateMutex);
        bool changed{};
        for (auto& task : impl_->state.tasks) {
            if (task.state == TaskState::Completed || task.state == TaskState::Cancelled || task.id == failedRecoveryTask) continue;
            task.state = TaskState::Paused;
            if (!hasResumeIdentity(task)) task.error = i18n::tr(i18n::TextId::PartialIdentityRestart);
            else if (!reconcileTask(task, error)) { task.state = TaskState::Failed; task.error = error; }
            else task.error = task.committedBytes == task.expectedSize
                ? i18n::tr(i18n::TextId::CompleteAwaitingVerification)
                : i18n::tr(i18n::TextId::InterruptedDownload);
            changed = true;
        }
        if (changed) impl_->saveLocked(error);
    }
    impl_->notify();
}

size_t AppController::subscribe(Observer observer) {
    std::scoped_lock lock(impl_->observerMutex);
    const auto id = impl_->nextObserver++;
    impl_->observers.emplace(id, std::move(observer));
    return id;
}

void AppController::unsubscribe(size_t id) {
    std::scoped_lock lock(impl_->observerMutex);
    impl_->observers.erase(id);
}

ui::HomeModel AppController::homeSnapshot() const {
    return ui::makeHomeModel(impl_->stateCopy(), impl_->applet, impl_->networkReady);
}

ui::TransfersModel AppController::transfersSnapshot() const {
    return ui::makeTransfersModel(impl_->stateCopy(), impl_->operation.snapshot());
}

ui::FilesModel AppController::filesSnapshot() const {
    std::scoped_lock lock(impl_->filesMutex);
    return impl_->files;
}

ui::LibraryModel AppController::librarySnapshot() const {
    return ui::makeLibraryModel(impl_->stateCopy(), impl_->applet);
}

ui::SettingsModel AppController::settingsSnapshot() const {
    return ui::makeSettingsModel(impl_->stateCopy());
}

ui::OperationSnapshot AppController::operationSnapshot() const { return impl_->operation.snapshot(); }

PairingModel AppController::pairingSnapshot() const {
    std::scoped_lock lock(impl_->pairingMutex);
    return impl_->pairing;
}

HomeStorageSetupModel AppController::homeStorageSetupSnapshot() const {
    std::scoped_lock lock(impl_->homeMutex);
    return impl_->homeSetup;
}

std::vector<ProviderOption> AppController::providersSnapshot() const {
    const auto state = impl_->stateCopy();
    std::vector<ProviderOption> providers;
    providers.reserve(state.providers.size());
    for (const auto& provider : state.providers) {
        std::string detail = provider.baseUrl;
        if (provider.kind == ProviderKind::GoogleDrive) {
            detail = i18n::tr(i18n::TextId::NoAccountConnected);
            for (const auto& account : state.accounts) if (account.id == state.lastAccountId) detail = account.email;
        }
        providers.push_back({provider.id,
            provider.kind == ProviderKind::GoogleDrive ? i18n::tr(i18n::TextId::MyDrive) : provider.name,
            detail, provider.kind == ProviderKind::GoogleDrive});
    }
    return providers;
}

void AppController::selectProvider(const std::string& id) {
    if (impl_->operation.snapshot().busy) return;
    {
        std::scoped_lock lock(impl_->stateMutex);
        const auto found = std::find_if(impl_->state.providers.begin(), impl_->state.providers.end(), [&](const ProviderConfig& value) { return value.id == id; });
        if (found == impl_->state.providers.end()) return;
        impl_->state.activeProviderId = id;
        impl_->folder = found->lastFolderId.empty() ? "root" : found->lastFolderId;
        std::string error;
        impl_->saveLocked(error);
    }
    impl_->parentFolders.clear();
    impl_->parentNames.clear();
    {
        std::scoped_lock lock(impl_->filesMutex);
        impl_->files.shared = false;
    }
    impl_->setFilesHeader();
    refreshFiles();
}

void AppController::setSharedWithMe(bool shared) {
    if (impl_->operation.snapshot().busy) return;
    {
        std::scoped_lock lock(impl_->filesMutex);
        if (!impl_->files.canChangeScope || impl_->files.shared == shared) return;
        impl_->files.shared = shared;
    }
    if (shared) {
        impl_->folder = "root";
    } else {
        const auto state = impl_->stateCopy();
        const auto provider = std::find_if(state.providers.begin(), state.providers.end(), [&](const ProviderConfig& value) {
            return value.id == state.activeProviderId;
        });
        impl_->folder = provider == state.providers.end() || provider->lastFolderId.empty()
            ? "root" : provider->lastFolderId;
    }
    impl_->parentFolders.clear();
    impl_->parentNames.clear();
    refreshFiles();
}

void AppController::refreshFiles() {
    if (!impl_->networkReady) {
        {
            std::scoped_lock lock(impl_->filesMutex);
            impl_->files.error = networkError(impl_->networkResult);
            impl_->files.loading = false;
        }
        impl_->notify();
        return;
    }
    impl_->launch(ui::OperationPhase::Preparing, i18n::TextId::Files, i18n::TextId::PreparingDownload, true,
        [this](uint64_t generation) {
            const auto state = impl_->stateCopy();
            const auto providerIt = std::find_if(state.providers.begin(), state.providers.end(), [&](const ProviderConfig& value) {
                return value.id == state.activeProviderId;
            });
            if (providerIt == state.providers.end()) {
                std::scoped_lock lock(impl_->filesMutex);
                impl_->files.error = i18n::tr(i18n::TextId::ConfigMissing);
                impl_->finish(generation, ui::OperationPhase::Failed, impl_->files.error);
                return;
            }
            {
                std::scoped_lock lock(impl_->filesMutex);
                impl_->files.loading = true;
                impl_->files.error.clear();
                impl_->files.entries.clear();
                impl_->files.cursor.clear();
                impl_->remoteFiles.clear();
            }
            impl_->notify();
            std::vector<RemoteEntry> entries;
            std::string cursor, error;
            bool ok{};
            if (providerIt->kind == ProviderKind::GoogleDrive) {
                if (state.lastAccountId.empty()) error = i18n::tr(i18n::TextId::ConnectAccountFirst);
                else {
                    std::string token;
                    AuthClient auth(impl_->http(generation), state.serviceUrl);
                    if (auth.accessToken(state.sessionToken, state.lastAccountId, token, error)) {
                        bool shared{};
                        { std::scoped_lock lock(impl_->filesMutex); shared = impl_->files.shared; }
                        ok = GoogleStorageProvider(impl_->http(generation), token).list(impl_->folder, shared, "", entries, cursor, error);
                    }
                }
            } else {
                ok = HomeStorageProvider(impl_->http(generation), *providerIt).list(impl_->folder, false, "", entries, cursor, error);
            }
            {
                std::scoped_lock lock(impl_->filesMutex);
                impl_->files.loading = false;
                impl_->files.error = ok ? "" : error;
                if (ok) {
                    impl_->remoteFiles = entries;
                    impl_->files.cursor = cursor;
                    for (const auto& entry : entries) impl_->files.entries.push_back(fileRow(entry, state));
                }
            }
            impl_->finish(generation, ok ? ui::OperationPhase::Completed : ui::OperationPhase::Failed,
                ok ? "" : error);
        });
}

void AppController::loadNextFilesPage() {
    std::string cursor;
    { std::scoped_lock lock(impl_->filesMutex); cursor = impl_->files.cursor; }
    if (cursor.empty() || impl_->operation.snapshot().busy) return;
    impl_->launch(ui::OperationPhase::Preparing, i18n::TextId::Files, i18n::TextId::Ellipsis, true,
        [this, cursor](uint64_t generation) {
            const auto state = impl_->stateCopy();
            const auto provider = std::find_if(state.providers.begin(), state.providers.end(), [&](const ProviderConfig& value) { return value.id == state.activeProviderId; });
            std::vector<RemoteEntry> page;
            std::string next, error, token;
            bool ok{};
            bool shared{};
            { std::scoped_lock lock(impl_->filesMutex); shared = impl_->files.shared; impl_->files.loading = true; }
            if (provider != state.providers.end() && provider->kind == ProviderKind::GoogleDrive &&
                AuthClient(impl_->http(generation), state.serviceUrl).accessToken(state.sessionToken, state.lastAccountId, token, error))
                ok = GoogleStorageProvider(impl_->http(generation), token).list(impl_->folder, shared, cursor, page, next, error);
            else if (provider != state.providers.end() && provider->kind == ProviderKind::HomeStorage)
                ok = HomeStorageProvider(impl_->http(generation), *provider).list(impl_->folder, false, cursor, page, next, error);
            {
                std::scoped_lock lock(impl_->filesMutex);
                impl_->files.loading = false;
                impl_->files.error = ok ? "" : error;
                if (ok) {
                    impl_->remoteFiles.insert(impl_->remoteFiles.end(), page.begin(), page.end());
                    for (const auto& entry : page) impl_->files.entries.push_back(fileRow(entry, state));
                    impl_->files.cursor = next;
                }
            }
            impl_->finish(generation, ok ? ui::OperationPhase::Completed : ui::OperationPhase::Failed, ok ? "" : error);
        });
}

void AppController::openFolder(size_t index) {
    RemoteEntry entry;
    {
        std::scoped_lock lock(impl_->filesMutex);
        if (index >= impl_->remoteFiles.size() || !impl_->remoteFiles[index].folder) return;
        entry = impl_->remoteFiles[index];
    }
    impl_->parentFolders.push_back(impl_->folder);
    impl_->parentNames.push_back(entry.name);
    impl_->folder = entry.id;
    impl_->persistFolder();
    impl_->setFilesHeader();
    refreshFiles();
}

void AppController::backFolder() {
    if (impl_->parentFolders.empty() || impl_->operation.snapshot().busy) return;
    impl_->folder = impl_->parentFolders.back();
    impl_->parentFolders.pop_back();
    if (!impl_->parentNames.empty()) impl_->parentNames.pop_back();
    impl_->persistFolder();
    impl_->setFilesHeader();
    refreshFiles();
}

void AppController::download(size_t index, bool installAfter, NspInstallStorage destination, bool restart) {
    RemoteEntry remote;
    {
        std::scoped_lock lock(impl_->filesMutex);
        if (index >= impl_->remoteFiles.size() || impl_->remoteFiles[index].folder || !impl_->remoteFiles[index].canDownload) return;
        remote = impl_->remoteFiles[index];
    }
    impl_->launch(ui::OperationPhase::Preparing, i18n::TextId::Transfers, i18n::TextId::PreparingDownload, true,
        [this, remote, installAfter, destination, restart](uint64_t generation) {
            State state = impl_->stateCopy();
            const std::string providerId = remote.providerId.empty() ? "google-drive" : remote.providerId;
            const auto provider = std::find_if(state.providers.begin(), state.providers.end(), [&](const ProviderConfig& value) { return value.id == providerId; });
            std::string token, error;
            if (provider == state.providers.end()) { impl_->finish(generation, ui::OperationPhase::Failed, i18n::tr(i18n::TextId::ConfigMissing)); return; }
            if (provider->kind == ProviderKind::GoogleDrive &&
                !AuthClient(impl_->http(generation), state.serviceUrl).accessToken(state.sessionToken, state.lastAccountId, token, error)) {
                impl_->finish(generation, ui::OperationPhase::Failed, error); return;
            }
            const std::string accountId = provider->kind == ProviderKind::GoogleDrive ? state.lastAccountId : "";
            Task task;
            const auto previous = std::find_if(state.tasks.begin(), state.tasks.end(), [&](const Task& value) {
                return value.providerId == providerId && value.accountId == accountId && value.remoteId == remote.id &&
                    value.state != TaskState::Completed && value.state != TaskState::Cancelled;
            });
            if (previous != state.tasks.end()) {
                task = *previous;
                if (restart) {
                    if (LocalFile::exists(task.localPath, task.storageKind) &&
                        !LocalFile::remove(task.localPath, task.storageKind, error)) {
                        impl_->finish(generation, ui::OperationPhase::Failed,
                            formatted(i18n::TextId::CannotDeletePartial, error));
                        return;
                    }
                    applyRemote(task, remote, accountId, impl_->store);
                } else if (!sameRemote(task, remote, accountId) || !hasResumeIdentity(task)) {
                    impl_->finish(generation, ui::OperationPhase::Failed, i18n::tr(i18n::TextId::RemoteVersionChanged));
                    return;
                }
            } else {
                task.id = makeId();
                applyRemote(task, remote, accountId, impl_->store);
            }
            task.installAfterDownload = installAfter;
            task.state = TaskState::Queued;
            if (!reconcileTask(task, error)) { task.state = TaskState::Failed; task.error = error; impl_->upsertTask(task, error); impl_->finish(generation, ui::OperationPhase::Failed, error); return; }
            if (!hasEnoughSpace(task.expectedSize - task.committedBytes)) {
                task.state = TaskState::Paused; task.error = i18n::tr(i18n::TextId::SdCardInsufficient);
                impl_->upsertTask(task, error); impl_->finish(generation, ui::OperationPhase::Paused, task.error); return;
            }
            LocalFile output;
            const bool opened = LocalFile::exists(task.localPath, task.storageKind)
                ? output.open(task.localPath, task.storageKind, true, error)
                : output.create(task.localPath, task.storageKind, error);
            if (!opened) { task.state = TaskState::Failed; task.error = error; impl_->upsertTask(task, error); impl_->finish(generation, ui::OperationPhase::Failed, error); return; }
            task.state = TaskState::Downloading;
            impl_->upsertTask(task, error);
            impl_->operation.setPhase(generation, ui::OperationPhase::Downloading, task.displayName, true);
            impl_->operation.update(generation, task.committedBytes, task.expectedSize);
            impl_->notify();
            const DownloadRequest request = provider->kind == ProviderKind::GoogleDrive
                ? GoogleStorageProvider(impl_->http(generation), token).downloadRequest(remote)
                : HomeStorageProvider(impl_->http(generation), *provider).downloadRequest(remote);
            TransferMeter meter(task.committedBytes);
            auto lastProgressAt = std::chrono::steady_clock::time_point{};
            uint64_t lastCheckpoint = task.committedBytes;
            auto lastCheckpointAt = std::chrono::steady_clock::now();
            DownloadResult result;
            const bool downloaded = task.committedBytes == task.expectedSize || impl_->http(generation).download(
                request.url, request.headers, output, task.committedBytes, task.expectedSize, task.etag,
                [&](const std::string& etag) { task.etag = etag; impl_->upsertTask(task, error); return error.empty(); },
                [&](uint64_t received) {
                    const auto now = std::chrono::steady_clock::now();
                    if (received == task.expectedSize || now - lastProgressAt >= kTransferUiInterval) {
                        const auto estimate = meter.sample(received, task.expectedSize, now);
                        impl_->operation.update(generation, received, task.expectedSize, estimate.bytesPerSecond, estimate.etaSeconds);
                        impl_->notify();
                        lastProgressAt = now;
                    }
                    if (received - lastCheckpoint >= kCheckpointBytes && now - lastCheckpointAt >= kCheckpointInterval) {
                        if (!output.flush(error) || !output.size(task.committedBytes, error)) return false;
                        impl_->upsertTask(task, error);
                        lastCheckpoint = task.committedBytes;
                        lastCheckpointAt = now;
                    }
                    return impl_->operation.shouldContinue(generation);
                }, result, error);
            output.flush(error);
            output.size(task.committedBytes, error);
            output.close();
            if (!downloaded) {
                task.state = result.status == DownloadStatus::RangeRejected ? TaskState::Failed : TaskState::Paused;
                task.error = error.empty() ? i18n::tr(i18n::TextId::DownloadPaused) : error;
                impl_->upsertTask(task, error);
                impl_->finish(generation, task.state == TaskState::Paused ? ui::OperationPhase::Paused : ui::OperationPhase::Failed, task.error);
                return;
            }
            task.state = TaskState::Verifying;
            impl_->upsertTask(task, error);
            impl_->operation.setPhase(generation, ui::OperationPhase::Verifying, i18n::tr(i18n::TextId::VerifyingDownload), true);
            impl_->operation.update(generation, 0, task.expectedSize);
            std::string digest;
            if (!impl_->digestFile(task, generation, digest, error) ||
                (!task.sha256.empty() && digest != task.sha256) || (!task.md5.empty() && digest != task.md5)) {
                if (error.empty()) error = i18n::tr(i18n::TextId::ChecksumMismatch);
                task.state = TaskState::Failed; task.error = error; impl_->upsertTask(task, error);
                impl_->finish(generation, ui::OperationPhase::Failed, error); return;
            }
            task.state = TaskState::Completed;
            task.committedBytes = task.expectedSize;
            task.localState = LocalState::Present;
            LibraryItem item;
            const auto existingItem = std::find_if(state.library.begin(), state.library.end(),
                [&](const LibraryItem& value) { return value.id == task.id; });
            if (existingItem != state.library.end()) item = *existingItem;
            item.id = task.id; item.providerId = task.providerId; item.accountId = task.accountId;
            item.remoteId = task.remoteId; item.name = task.displayName; item.localPath = task.localPath;
            item.md5 = task.md5; item.sha256 = task.sha256; item.size = task.expectedSize;
            item.localState = LocalState::Present; item.storageKind = task.storageKind;
            impl_->upsertTaskAndLibrary(task, item, error);
            if (task.installAfterDownload && !impl_->applet) {
                const bool installed = impl_->installDownloaded(task, item, destination, generation, error);
                impl_->finish(generation, installed ? ui::OperationPhase::Completed : ui::OperationPhase::Failed,
                    installed && error.empty() ? i18n::tr(i18n::TextId::InstallComplete) : error);
                return;
            }
            impl_->finish(generation, ui::OperationPhase::Completed, i18n::tr(i18n::TextId::DownloadComplete));
        });
}

void AppController::hide(size_t index) {
    RemoteEntry entry;
    ProviderConfig provider;
    {
        std::scoped_lock lock(impl_->filesMutex);
        if (index >= impl_->remoteFiles.size() || !impl_->remoteFiles[index].canHide) return;
        entry = impl_->remoteFiles[index];
    }
    provider = impl_->providerCopy(entry.providerId);
    impl_->launch(ui::OperationPhase::Removing, i18n::TextId::HideCatalogEntry, i18n::TextId::Ellipsis, true,
        [this, provider, entry](uint64_t generation) {
            std::string error;
            const bool ok = HomeStorageClient(impl_->http(generation)).hide(provider, entry.id, error);
            impl_->finish(generation, ok ? ui::OperationPhase::Completed : ui::OperationPhase::Failed, ok ? "" : error);
            if (ok) {
                std::scoped_lock lock(impl_->filesMutex);
                for (size_t index = 0; index < impl_->remoteFiles.size(); ++index) {
                    if (impl_->remoteFiles[index].id != entry.id) continue;
                    impl_->remoteFiles.erase(impl_->remoteFiles.begin() + static_cast<std::ptrdiff_t>(index));
                    impl_->files.entries.erase(impl_->files.entries.begin() + static_cast<std::ptrdiff_t>(index));
                    break;
                }
            }
        });
}

void AppController::beginPairing() {
    {
        std::scoped_lock lock(impl_->pairingMutex);
        impl_->pairing = {};
        impl_->pairing.loading = true;
    }
    impl_->launch(ui::OperationPhase::Pairing, i18n::TextId::ConnectDrive, i18n::TextId::AwaitingAuthorization, true,
        [this](uint64_t generation) {
            const auto state = impl_->stateCopy();
            if (!impl_->networkReady || state.serviceUrl.empty()) {
                const std::string error = !impl_->networkReady ? networkError(impl_->networkResult) : i18n::tr(i18n::TextId::ConfigMissing);
                { std::scoped_lock lock(impl_->pairingMutex); impl_->pairing.loading = false; impl_->pairing.error = error; }
                impl_->finish(generation, ui::OperationPhase::Failed, error);
                return;
            }
            std::string consoleKey = state.consolePublicKey.empty() ? makeId() + makeId() : state.consolePublicKey;
            std::string id, url, qrUrl, code, secret, error;
            const bool ok = AuthClient(impl_->http(generation), state.serviceUrl).begin(consoleKey, id, url, qrUrl, code, secret, error);
            {
                std::scoped_lock lock(impl_->pairingMutex);
                impl_->pairing = {url, qrUrl, code, error, false, ok};
                impl_->pairingId = id;
                impl_->pairingSecret = secret;
            }
            if (ok) {
                std::scoped_lock lock(impl_->stateMutex);
                impl_->state.consolePublicKey = consoleKey;
                impl_->saveLocked(error);
            }
            impl_->finish(generation, ok ? ui::OperationPhase::Completed : ui::OperationPhase::Failed, ok ? "" : error);
        });
}

void AppController::checkPairing() {
    PairingModel model;
    std::string id, secret;
    { std::scoped_lock lock(impl_->pairingMutex); model = impl_->pairing; id = impl_->pairingId; secret = impl_->pairingSecret; }
    if (!model.ready) return;
    impl_->launch(ui::OperationPhase::Pairing, i18n::TextId::ConnectDrive, i18n::TextId::AwaitingAuthorization, true,
        [this, id, secret](uint64_t generation) {
            const auto state = impl_->stateCopy();
            Account account;
            std::string session, error;
            AuthClient auth(impl_->http(generation), state.serviceUrl);
            const bool ok = auth.poll(id, secret, account, error) && auth.claim(id, secret, session, account, error);
            if (ok) {
                std::scoped_lock lock(impl_->stateMutex);
                const auto found = std::find_if(impl_->state.accounts.begin(), impl_->state.accounts.end(), [&](const Account& value) { return value.id == account.id; });
                if (found == impl_->state.accounts.end()) impl_->state.accounts.push_back(account); else *found = account;
                impl_->state.lastAccountId = account.id;
                impl_->state.sessionToken = session;
                impl_->saveLocked(error);
            } else {
                std::scoped_lock lock(impl_->pairingMutex);
                impl_->pairing.error = error;
            }
            impl_->finish(generation, ok ? ui::OperationPhase::Completed : ui::OperationPhase::Failed,
                ok ? formatted(i18n::TextId::Connected, account.email) : error);
        });
}

void AppController::cancelPairing() {
    impl_->operation.requestCancel();
    std::scoped_lock lock(impl_->pairingMutex);
    impl_->pairing = {};
}

void AppController::setLanguage(i18n::Language language) {
    if (impl_->operation.snapshot().busy) return;
    std::string error;
    { std::scoped_lock lock(impl_->stateMutex); impl_->state.language = i18n::languageCode(language); impl_->saveLocked(error); }
    impl_->notify();
}

void AppController::discoverHomeStorageServers() {
    { std::scoped_lock lock(impl_->homeMutex); impl_->homeSetup = {}; impl_->homeSetup.loading = true; }
    impl_->launch(ui::OperationPhase::Discovering, i18n::TextId::DetectNetwork, i18n::TextId::DetectingStorage, true,
        [this](uint64_t generation) {
            (void)generation;
            std::vector<DiscoveredHomeStorage> found;
            std::string error;
            const bool ok = discoverHomeStorage(found, error);
            { std::scoped_lock lock(impl_->homeMutex); impl_->homeSetup = {found, error, false}; }
            impl_->finish(generation, ok ? ui::OperationPhase::Completed : ui::OperationPhase::Failed, ok ? "" : error);
        });
}

void AppController::configureHomeStorage(const std::string& input, const std::string& username, const std::string& password) {
    impl_->launch(ui::OperationPhase::Discovering, i18n::TextId::HomeStorage, i18n::TextId::DetectingStorage, true,
        [this, input, username, password](uint64_t generation) {
            std::string address, token, error;
            bool canManage{};
            if (!normalizeHomeStorageUrl(input, address)) { impl_->finish(generation, ui::OperationPhase::Failed, i18n::tr(i18n::TextId::InvalidAddress)); return; }
            HomeStorageClient client(impl_->http(generation));
            HomeStorageHealth health;
            bool ok = client.health(address, health, error);
            if (ok && health.authRequired) ok = client.authenticate(address, username, password, token, canManage, error);
            if (ok) {
                std::scoped_lock lock(impl_->stateMutex);
                const std::string id = "home-" + health.instanceId;
                auto provider = std::find_if(impl_->state.providers.begin(), impl_->state.providers.end(), [&](const ProviderConfig& value) { return value.id == id; });
                if (provider == impl_->state.providers.end()) { impl_->state.providers.push_back({}); provider = std::prev(impl_->state.providers.end()); }
                *provider = {id, health.name, address, token, "root", ProviderKind::HomeStorage, canManage};
                impl_->state.activeProviderId = id;
                impl_->saveLocked(error);
            }
            impl_->finish(generation, ok ? ui::OperationPhase::Completed : ui::OperationPhase::Failed,
                ok ? formatted(i18n::TextId::ProviderConnected, health.name) : error);
        });
}

void AppController::installLibraryItem(size_t index, NspInstallStorage destination) {
    if (impl_->applet) return;
    const auto state = impl_->stateCopy();
    if (index >= state.library.size()) return;
    auto item = state.library[index];
    const auto taskIt = std::find_if(state.tasks.begin(), state.tasks.end(), [&](const Task& value) { return value.id == item.id; });
    if (taskIt == state.tasks.end() || item.localState != LocalState::Present) return;
    auto task = *taskIt;
    impl_->launch(ui::OperationPhase::Installing, i18n::TextId::InstallNsp, i18n::TextId::Ellipsis, false,
        [this, task, item, destination](uint64_t generation) mutable {
            std::string error;
            const bool installed = impl_->installDownloaded(task, item, destination, generation, error);
            impl_->finish(generation, installed ? ui::OperationPhase::Completed : ui::OperationPhase::Failed,
                installed && error.empty() ? i18n::tr(i18n::TextId::InstallComplete) : error);
        });
}

void AppController::removeLibraryPackage(size_t index) {
    if (impl_->operation.snapshot().busy) return;
    std::string error;
    { std::scoped_lock lock(impl_->stateMutex); if (index >= impl_->state.library.size()) return; impl_->store.removeDownload(impl_->state, impl_->state.library[index].id, error); }
    impl_->notify();
}

void AppController::cancelOperation() { impl_->operation.requestCancel(); impl_->notify(); }
bool AppController::appletMode() const { return impl_->applet; }

} // namespace switchdrive
