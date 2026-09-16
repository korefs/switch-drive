#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "switchdrive/core.hpp"

namespace switchdrive::ui {

enum class OperationPhase {
    Idle, Preparing, Downloading, Verifying, Installing, Removing,
    Discovering, Pairing, Completed, Paused, Failed, Cancelled
};

struct OperationSnapshot {
    OperationPhase phase{OperationPhase::Idle};
    std::string title;
    std::string message;
    uint64_t current{};
    uint64_t total{};
    double bytesPerSecond{};
    uint64_t etaSeconds{};
    uint64_t generation{};
    bool busy{};
    bool cancellable{};
    bool cancelRequested{};
};

struct HomeModel {
    std::string account;
    std::string provider;
    size_t activeTasks{};
    size_t libraryItems{};
    bool appletMode{};
    bool networkReady{};
};

struct FileRowModel {
    std::string id;
    std::string title;
    std::string detail;
    bool folder{};
    bool installable{};
    bool partial{};
    bool restartRequired{};
    bool canDownload{};
    bool canHide{};
};

struct FilesModel {
    std::string providerId;
    std::string providerName;
    std::string location;
    std::vector<std::string> breadcrumb;
    std::vector<FileRowModel> entries;
    std::string cursor;
    std::string error;
    bool shared{};
    bool loading{};
    bool canChangeScope{};
};

struct LibraryRowModel {
    std::string id;
    std::string title;
    std::string detail;
    bool available{};
    bool installable{};
    bool installed{};
    bool managed{};
    bool canInstall{};
    bool canRemovePackage{};
    bool canUninstall{};
};

struct LibraryModel {
    std::vector<LibraryRowModel> entries;
    bool appletMode{};
};

struct SettingsModel {
    std::string account;
    std::string language;
    std::string languageCode;
    std::string homeStorage;
    bool deleteAfterInstall{};
};

HomeModel makeHomeModel(const State& state, bool appletMode, bool networkReady);
LibraryModel makeLibraryModel(const State& state, bool appletMode);
SettingsModel makeSettingsModel(const State& state);

// Serializes long-running commands and gives views a generation token. A stale
// view can safely ignore snapshots whose generation no longer matches.
class OperationGate {
  public:
    uint64_t start(OperationPhase phase, std::string title, std::string message, bool cancellable);
    bool update(uint64_t generation, uint64_t current, uint64_t total, double bytesPerSecond = 0, uint64_t etaSeconds = 0);
    bool setPhase(uint64_t generation, OperationPhase phase, std::string message, bool cancellable);
    bool finish(uint64_t generation, OperationPhase phase, std::string message = {});
    bool requestCancel();
    bool shouldContinue(uint64_t generation) const;
    OperationSnapshot snapshot() const;

  private:
    mutable std::mutex mutex_;
    OperationSnapshot snapshot_;
    uint64_t nextGeneration_{1};
};

} // namespace switchdrive::ui
