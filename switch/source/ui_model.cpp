#include "switchdrive/ui_model.hpp"
#include "switchdrive/i18n.hpp"
#include "switchdrive/network.hpp"

#include <algorithm>
#include <cstdio>

namespace switchdrive::ui {

namespace {

std::string accountName(const State& state) {
    for (const auto& account : state.accounts)
        if (account.id == state.lastAccountId) return account.email;
    return i18n::tr(i18n::TextId::NoAccountConnected);
}

std::string providerName(const State& state) {
    for (const auto& provider : state.providers) {
        if (provider.id != state.activeProviderId) continue;
        return provider.kind == ProviderKind::GoogleDrive
            ? i18n::tr(i18n::TextId::MyDrive)
            : provider.name;
    }
    return i18n::tr(i18n::TextId::MyDrive);
}

std::string itemDetail(const LibraryItem& item) {
    if (item.localState != LocalState::Present)
        return i18n::tr(i18n::TextId::MissingFile);
    char text[64]{};
    std::snprintf(text, sizeof(text), i18n::tr(i18n::TextId::FileSize),
        static_cast<double>(item.size) / (1024.0 * 1024.0));
    return text;
}

bool activeTransfer(TaskState state) {
    return state == TaskState::Queued || state == TaskState::Downloading ||
        state == TaskState::Verifying || state == TaskState::Installing || state == TaskState::Paused;
}

bool matchesOperation(TaskState state, OperationPhase phase) {
    switch (phase) {
        case OperationPhase::Preparing: return state == TaskState::Queued;
        case OperationPhase::Downloading: return state == TaskState::Downloading;
        case OperationPhase::Verifying: return state == TaskState::Verifying;
        case OperationPhase::Installing: return state == TaskState::Installing;
        default: return false;
    }
}

std::string transferDetail(const Task& task, const OperationSnapshot& operation, bool current) {
    switch (task.state) {
        case TaskState::Queued:
            return i18n::tr(i18n::TextId::PreparingDownload);
        case TaskState::Downloading: {
            const uint64_t received = current && operation.total ? operation.current : task.committedBytes;
            const uint64_t total = current && operation.total ? operation.total : task.expectedSize;
            if (!total) return i18n::tr(i18n::TextId::ResumingDownload);
            const TransferEstimate estimate = current
                ? TransferEstimate{operation.bytesPerSecond, operation.etaSeconds, operation.bytesPerSecond > 0}
                : TransferEstimate{};
            return formatTransferProgress(received, total, estimate);
        }
        case TaskState::Paused:
            return task.error.empty() ? i18n::tr(i18n::TextId::Paused) : task.error;
        case TaskState::Verifying:
            return i18n::tr(i18n::TextId::VerifyingDownload);
        case TaskState::Installing:
            return i18n::tr(i18n::TextId::InstallNsp);
        default:
            return {};
    }
}

} // namespace

HomeModel makeHomeModel(const State& state, bool appletMode, bool networkReady) {
    HomeModel model;
    model.account = accountName(state);
    model.provider = providerName(state);
    model.activeTasks = static_cast<size_t>(std::count_if(state.tasks.begin(), state.tasks.end(),
        [](const Task& task) { return activeTransfer(task.state); }));
    model.libraryItems = state.library.size();
    model.appletMode = appletMode;
    model.networkReady = networkReady;
    return model;
}

TransfersModel makeTransfersModel(const State& state, const OperationSnapshot& operation) {
    TransfersModel model;
    model.busy = operation.busy && operation.title == i18n::tr(i18n::TextId::Transfers);
    model.cancellable = model.busy && operation.cancellable;
    const Task* current{};
    if (model.busy) {
        const auto found = std::find_if(state.tasks.begin(), state.tasks.end(), [&](const Task& task) {
            return matchesOperation(task.state, operation.phase);
        });
        if (found != state.tasks.end()) current = &*found;
    }
    for (const auto& task : state.tasks) {
        if (!activeTransfer(task.state)) continue;
        model.entries.push_back({task.id,
            task.displayName.empty() ? i18n::tr(i18n::TextId::Transfers) : task.displayName,
            transferDetail(task, operation, &task == current), task.state});
    }
    return model;
}

LibraryModel makeLibraryModel(const State& state, bool appletMode) {
    LibraryModel model;
    model.appletMode = appletMode;
    model.entries.reserve(state.library.size());
    for (const auto& item : state.library) {
        const bool available = item.localState == LocalState::Present && LocalFile::exists(item.localPath, item.storageKind);
        const bool installable = isInstallablePackage(item.name) || isNro(item.name);
        model.entries.push_back({
            item.id, item.name, itemDetail(item), available, installable,
            available && installable && !appletMode,
            available,
        });
    }
    return model;
}

SettingsModel makeSettingsModel(const State& state) {
    SettingsModel model;
    model.account = accountName(state);
    model.language = std::string(i18n::languageName(i18n::parseLanguage(state.language)));
    model.languageCode = std::string(i18n::languageCode(i18n::parseLanguage(state.language)));
    model.deleteAfterInstall = state.deleteAfterInstall;
    const auto home = std::find_if(state.providers.begin(), state.providers.end(), [](const ProviderConfig& provider) {
        return provider.kind == ProviderKind::HomeStorage;
    });
    model.homeStorage = home == state.providers.end()
        ? i18n::tr(i18n::TextId::NoStorageFound)
        : home->name;
    return model;
}

uint64_t OperationGate::start(OperationPhase phase, std::string title, std::string message, bool cancellable) {
    std::scoped_lock lock(mutex_);
    if (snapshot_.busy) return 0;
    snapshot_ = {phase, std::move(title), std::move(message), 0, 0, 0, 0,
        nextGeneration_++, true, cancellable, false};
    return snapshot_.generation;
}

bool OperationGate::update(uint64_t generation, uint64_t current, uint64_t total, double bytesPerSecond, uint64_t etaSeconds) {
    std::scoped_lock lock(mutex_);
    if (!snapshot_.busy || snapshot_.generation != generation) return false;
    snapshot_.current = current;
    snapshot_.total = total;
    snapshot_.bytesPerSecond = bytesPerSecond;
    snapshot_.etaSeconds = etaSeconds;
    return true;
}

bool OperationGate::setPhase(uint64_t generation, OperationPhase phase, std::string message, bool cancellable) {
    std::scoped_lock lock(mutex_);
    if (!snapshot_.busy || snapshot_.generation != generation) return false;
    snapshot_.phase = phase;
    snapshot_.message = std::move(message);
    snapshot_.cancellable = cancellable;
    return true;
}

bool OperationGate::finish(uint64_t generation, OperationPhase phase, std::string message) {
    std::scoped_lock lock(mutex_);
    if (!snapshot_.busy || snapshot_.generation != generation) return false;
    snapshot_.phase = phase;
    snapshot_.message = std::move(message);
    snapshot_.busy = false;
    snapshot_.cancellable = false;
    return true;
}

bool OperationGate::requestCancel() {
    std::scoped_lock lock(mutex_);
    if (!snapshot_.busy || !snapshot_.cancellable) return false;
    snapshot_.cancelRequested = true;
    return true;
}

bool OperationGate::shouldContinue(uint64_t generation) const {
    std::scoped_lock lock(mutex_);
    return snapshot_.busy && snapshot_.generation == generation && !snapshot_.cancelRequested;
}

OperationSnapshot OperationGate::snapshot() const {
    std::scoped_lock lock(mutex_);
    return snapshot_;
}

} // namespace switchdrive::ui
