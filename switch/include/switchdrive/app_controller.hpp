#pragma once

#include "switchdrive/core.hpp"
#include "switchdrive/i18n.hpp"
#include "switchdrive/network.hpp"
#include "switchdrive/ui_model.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace switchdrive {

struct ProviderOption {
    std::string id;
    std::string name;
    std::string detail;
    bool googleDrive{};
};

struct PairingModel {
    std::string url;
    std::string qrUrl;
    std::string code;
    std::string error;
    bool loading{};
    bool ready{};
};

struct HomeStorageSetupModel {
    std::vector<DiscoveredHomeStorage> discoveries;
    std::string error;
    bool loading{};
};

class AppController {
  public:
    using Observer = std::function<void()>;

    AppController(std::filesystem::path root, bool appletMode);
    ~AppController();
    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    void setNetworkStatus(bool ready, uint32_t result);
    void recover();

    size_t subscribe(Observer observer);
    void unsubscribe(size_t id);

    ui::HomeModel homeSnapshot() const;
    ui::TransfersModel transfersSnapshot() const;
    ui::FilesModel filesSnapshot() const;
    ui::LibraryModel librarySnapshot() const;
    ui::SettingsModel settingsSnapshot() const;
    ui::OperationSnapshot operationSnapshot() const;
    PairingModel pairingSnapshot() const;
    HomeStorageSetupModel homeStorageSetupSnapshot() const;
    std::vector<ProviderOption> providersSnapshot() const;

    void selectProvider(const std::string& id);
    void setSharedWithMe(bool shared);
    void refreshFiles();
    void loadNextFilesPage();
    void openFolder(size_t index);
    void backFolder();
    void download(size_t index, bool installAfter, NspInstallStorage destination = NspInstallStorage::SdCard,
        bool restart = false);
    void hide(size_t index);

    void beginPairing();
    void checkPairing();
    void cancelPairing();

    void setDeleteAfterInstall(bool enabled);
    void setLanguage(i18n::Language language);
    void discoverHomeStorageServers();
    void configureHomeStorage(const std::string& address, const std::string& username, const std::string& password);

    void installLibraryItem(size_t index, NspInstallStorage destination);
    void removeLibraryPackage(size_t index);

    void cancelOperation();
    bool appletMode() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace switchdrive
