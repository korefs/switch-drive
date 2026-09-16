#include "switchdrive/ui.hpp"

#include "switchdrive/app_controller.hpp"
#include "switchdrive/i18n.hpp"
#include "switchdrive/network.hpp"
#include "switchdrive/qr.hpp"

#include <borealis.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace switchdrive::ui {
namespace {

using Row = std::pair<std::string, std::string>;

brls::Label* label(const std::string& text, float size = 22, float height = 48) {
    auto* value = new brls::Label();
    value->setText(text);
    value->setFontSize(size);
    value->setHeight(height);
    value->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    value->setVerticalAlign(brls::VerticalAlign::CENTER);
    value->setSingleLine(true);
    return value;
}

void confirm(const std::string& message, std::function<void()> accepted) {
    auto* dialog = new brls::Dialog(message);
    dialog->addButton(i18n::tr(i18n::TextId::Cancel), [] {});
    dialog->addButton(i18n::tr(i18n::TextId::Confirm), std::move(accepted));
    dialog->open();
}

class FocusDetailCell final : public brls::DetailCell {
  public:
    void setFocusAction(std::function<void(size_t)> action) { focusAction = std::move(action); }

    void onFocusGained() override {
        brls::DetailCell::onFocusGained();
        const auto index = getIndexPath().row;
        if (focusAction && index >= 0) focusAction(static_cast<size_t>(index));
    }

  private:
    std::function<void(size_t)> focusAction;
};

class ModelDataSource final : public brls::RecyclerDataSource {
  public:
    using Rows = std::function<std::vector<Row>()>;
    using Select = std::function<void(size_t)>;
    using NearEnd = std::function<void()>;
    using Focus = std::function<void(size_t)>;

    ModelDataSource(Rows rows, Select select, NearEnd nearEnd = {}, Focus focus = {})
        : rows_(std::move(rows)), select_(std::move(select)), nearEnd_(std::move(nearEnd)), focus_(std::move(focus)) {}

    int numberOfRows(brls::RecyclerFrame*, int) override { return static_cast<int>(rows_().size()); }

    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override {
        auto rows = rows_();
        auto* cell = static_cast<brls::DetailCell*>(recycler->dequeueReusableCell("detail"));
        if (auto* focusCell = dynamic_cast<FocusDetailCell*>(cell)) focusCell->setFocusAction(focus_);
        if (index.row >= 0 && static_cast<size_t>(index.row) < rows.size()) {
            cell->setText(rows[static_cast<size_t>(index.row)].first);
            cell->setDetailText(rows[static_cast<size_t>(index.row)].second);
            if (nearEnd_ && static_cast<size_t>(index.row + 3) >= rows.size()) nearEnd_();
        }
        return cell;
    }

    void didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) override {
        if (index.row >= 0 && select_) select_(static_cast<size_t>(index.row));
    }

    float heightForRow(brls::RecyclerFrame*, brls::IndexPath) override { return 64; }

  private:
    Rows rows_;
    Select select_;
    NearEnd nearEnd_;
    Focus focus_;
};

class ObservedBox : public brls::Box {
  public:
    explicit ObservedBox(AppController& controller)
        : brls::Box(brls::Axis::COLUMN), controller(controller), alive(std::make_shared<std::atomic<bool>>(true)) {
        auto guard = alive;
        subscription = controller.subscribe([this, guard] {
            brls::sync([this, guard] {
                if (guard->load()) refresh();
            });
        });
    }

    ~ObservedBox() override {
        alive->store(false);
        controller.unsubscribe(subscription);
    }

    virtual void refresh() = 0;

  protected:
    AppController& controller;

  private:
    size_t subscription{};
    std::shared_ptr<std::atomic<bool>> alive;
};

class QrView final : public brls::View {
  public:
    void setContent(const std::string& value) {
        code = qr::encode(value);
        invalidate();
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style, brls::FrameContext*) override {
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgFill(vg);
        if (!code) return;
        const float quiet = 4;
        const float scale = std::min(width, height) / (code->size + quiet * 2);
        const float left = x + (width - scale * code->size) / 2;
        const float top = y + (height - scale * code->size) / 2;
        nvgBeginPath(vg);
        for (int row = 0; row < code->size; ++row)
            for (int column = 0; column < code->size; ++column)
                if (code->modules[static_cast<size_t>(row * code->size + column)])
                    nvgRect(vg, left + column * scale, top + row * scale, scale + 0.25f, scale + 0.25f);
        nvgFillColor(vg, nvgRGB(0, 0, 0));
        nvgFill(vg);
    }

  private:
    std::optional<qr::Code> code;
};

class ProgressBar final : public brls::View {
  public:
    void setProgress(float value) { progress = std::clamp(value, 0.0f, 1.0f); invalidate(); }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style, brls::FrameContext*) override {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, height / 2);
        nvgFillColor(vg, brls::Application::getTheme().getColor("brls/sidebar/separator"));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width * progress, height, height / 2);
        nvgFillColor(vg, brls::Application::getTheme().getColor("switchdrive/accent"));
        nvgFill(vg);
    }

  private:
    float progress{};
};

size_t focusedRow() {
    auto* cell = dynamic_cast<brls::RecyclerCell*>(brls::Application::getCurrentFocus());
    return cell && cell->getIndexPath().row >= 0 ? static_cast<size_t>(cell->getIndexPath().row) : static_cast<size_t>(-1);
}

void pushOperation(AppController& controller);
void pushPairing(AppController& controller);
void pushHomeStorage(AppController& controller);

class OperationView final : public ObservedBox {
  public:
    explicit OperationView(AppController& appController) : ObservedBox(appController) {
        setPadding(48, 72, 48, 72);
        phase = label("", 28, 70);
        message = label("", 22, 80);
        progress = new ProgressBar();
        progress->setHeight(12);
        detail = label("", 18, 64);
        addView(phase);
        addView(message);
        addView(progress);
        addView(detail);
        addView(new brls::Padding());
        registerAction(i18n::tr(i18n::TextId::Cancel), brls::BUTTON_B, [this](brls::View*) {
            const auto snapshot = controller.operationSnapshot();
            if (snapshot.busy && snapshot.cancellable) controller.cancelOperation();
            else if (!snapshot.busy) brls::Application::popActivity();
            return true;
        }, false, false, brls::SOUND_BACK);
        refresh();
    }

    void refresh() override {
        const auto snapshot = controller.operationSnapshot();
        setActionAvailable(brls::BUTTON_B, !snapshot.busy || snapshot.cancellable);
        phase->setText(snapshot.title);
        message->setText(snapshot.message);
        progress->setProgress(snapshot.total ? static_cast<float>(snapshot.current) / snapshot.total : 0);
        detail->setText(snapshot.total
            ? formatTransferProgress(snapshot.current, snapshot.total,
                {snapshot.bytesPerSecond, snapshot.etaSeconds, snapshot.bytesPerSecond > 0})
            : "");
    }

  private:
    brls::Label* phase{};
    brls::Label* message{};
    brls::Label* detail{};
    ProgressBar* progress{};
};

void pushFramed(const std::string& title, brls::View* content) {
    auto* frame = new brls::AppletFrame(content);
    frame->setTitle(title);
    brls::Application::pushActivity(new brls::Activity(frame));
}

void pushOperation(AppController& controller) {
    pushFramed(i18n::tr(i18n::TextId::Transfers), new OperationView(controller));
}

class PairingView final : public ObservedBox {
  public:
    explicit PairingView(AppController& appController) : ObservedBox(appController) {
        setPadding(28, 56, 28, 56);
        setAlignItems(brls::AlignItems::CENTER);
        subtitle = label(i18n::tr(i18n::TextId::ScanWithPhone), 24, 48);
        subtitle->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        qrView = new QrView();
        qrView->setWidth(360);
        qrView->setHeight(360);
        url = label("", 20, 50);
        url->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        code = label("", 28, 58);
        code->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        error = label("", 18, 48);
        error->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        addView(subtitle);
        addView(qrView);
        addView(url);
        addView(code);
        addView(error);
        registerAction(i18n::tr(i18n::TextId::CheckNow), brls::BUTTON_A, [this](brls::View*) { controller.checkPairing(); pushOperation(controller); return true; }, false, false, brls::SOUND_CLICK);
        registerAction(i18n::tr(i18n::TextId::Cancel), brls::BUTTON_B, [this](brls::View*) { controller.cancelPairing(); brls::Application::popActivity(); return true; }, false, false, brls::SOUND_BACK);
        refresh();
    }

    void refresh() override {
        const auto model = controller.pairingSnapshot();
        qrView->setContent(model.qrUrl);
        url->setText(model.url);
        code->setText(model.code.empty() ? "" : std::string(i18n::tr(i18n::TextId::Code)) + ": " + model.code);
        error->setText(model.error);
    }

  private:
    brls::Label* subtitle{};
    QrView* qrView{};
    brls::Label* url{};
    brls::Label* code{};
    brls::Label* error{};
};

void pushPairing(AppController& controller) {
    pushFramed(i18n::tr(i18n::TextId::ConnectDrive), new PairingView(controller));
    controller.beginPairing();
}

class HomeStorageView final : public ObservedBox {
  public:
    explicit HomeStorageView(AppController& appController) : ObservedBox(appController) {
        setPadding(24, 48, 24, 48);
        recycler = new brls::RecyclerFrame();
        recycler->setGrow(1);
        recycler->registerCell("detail", [] { return new brls::DetailCell(); });
        recycler->setDataSource(new ModelDataSource(
            [this] { return rows(); },
            [this](size_t index) { select(index); }));
        addView(recycler);
        refresh();
    }

    void refresh() override { recycler->reloadData(); }

  private:
    brls::RecyclerFrame* recycler{};

    std::vector<Row> rows() const {
        std::vector<Row> values{
            {i18n::tr(i18n::TextId::DetectNetwork), i18n::tr(i18n::TextId::DetectingStorage)},
            {i18n::tr(i18n::TextId::ManualSetup), i18n::tr(i18n::TextId::ServerAddress)},
        };
        const auto setup = controller.homeStorageSetupSnapshot();
        for (const auto& item : setup.discoveries) values.emplace_back(item.health.name, item.baseUrl);
        if (!setup.error.empty()) values.emplace_back(i18n::tr(i18n::TextId::HomeStorage), setup.error);
        return values;
    }

    void credentials(const std::string& address) {
        brls::Application::getImeManager()->openForText([this, address](std::string username) {
            brls::Application::getImeManager()->openForText([this, address, username](std::string password) {
                controller.configureHomeStorage(address, username, password);
                pushOperation(controller);
            }, i18n::tr(i18n::TextId::Password), "", 128, "");
        }, i18n::tr(i18n::TextId::Username), "", 128, "");
    }

    void select(size_t index) {
        if (index == 0) { controller.discoverHomeStorageServers(); pushOperation(controller); return; }
        if (index == 1) {
            brls::Application::getImeManager()->openForText([this](std::string address) { credentials(address); },
                i18n::tr(i18n::TextId::ServerAddress), "", 512, "");
            return;
        }
        const auto setup = controller.homeStorageSetupSnapshot();
        if (index - 2 < setup.discoveries.size()) credentials(setup.discoveries[index - 2].baseUrl);
    }
};

void pushHomeStorage(AppController& controller) {
    pushFramed(i18n::tr(i18n::TextId::HomeStorage), new HomeStorageView(controller));
}

class HomeView final : public ObservedBox {
  public:
    HomeView(AppController& appController, std::function<void(int)> navigateAction)
        : ObservedBox(appController), navigate(std::move(navigateAction)) {
        setPadding(24, 48, 24, 48);
        warning = label("", 18, 44);
        warning->setTextColor(nvgRGB(230, 140, 30));
        recycler = new brls::RecyclerFrame();
        recycler->setGrow(1);
        recycler->registerCell("detail", [] { return new brls::DetailCell(); });
        recycler->setDataSource(new ModelDataSource([this] { return rows(); }, [this](size_t index) {
            if (index == 0) pushPairing(controller);
            else if (index == 1) navigate(1);
            else if (index == 2) pushOperation(controller);
            else if (index == 3) navigate(2);
        }));
        addView(warning);
        addView(recycler);
        refresh();
    }

    void refresh() override {
        const auto model = controller.homeSnapshot();
        warning->setText(model.appletMode ? i18n::tr(i18n::TextId::AppletModeWarning) : "");
        warning->setVisibility(model.appletMode ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        recycler->reloadData();
    }

  private:
    brls::Label* warning{};
    brls::RecyclerFrame* recycler{};
    std::function<void(int)> navigate;

    std::vector<Row> rows() const {
        const auto model = controller.homeSnapshot();
        return {
            {i18n::tr(i18n::TextId::ConnectDrive), model.account},
            {i18n::tr(i18n::TextId::StorageProviders), model.provider},
            {i18n::tr(i18n::TextId::Transfers), std::to_string(model.activeTasks)},
            {i18n::tr(i18n::TextId::Library), std::to_string(model.libraryItems)},
        };
    }
};

class FilesView final : public ObservedBox {
  public:
    explicit FilesView(AppController& appController) : ObservedBox(appController) {
        setPadding(24, 48, 24, 48);
        recycler = new brls::RecyclerFrame();
        recycler->setGrow(1);
        recycler->registerCell("detail", [] { return new FocusDetailCell(); });
        recycler->setDataSource(new ModelDataSource([this] { return rows(); }, [this](size_t index) { select(index); },
            [this] { controller.loadNextFilesPage(); }, [this](size_t index) { updateActions(index); }));
        addView(recycler);
        recycler->registerAction(i18n::tr(i18n::TextId::StorageProviders), brls::BUTTON_X, [this](brls::View*) {
            chooseProvider();
            return true;
        }, false, false, brls::SOUND_CLICK);
        recycler->registerAction(i18n::tr(i18n::TextId::Files), brls::BUTTON_Y, [this](brls::View*) {
            chooseScope();
            return true;
        }, false, false, brls::SOUND_CLICK);
        recycler->registerAction(i18n::tr(i18n::TextId::HideCatalogEntry), brls::BUTTON_LT, [this](brls::View*) {
            const auto index = focusedRow();
            const auto model = controller.filesSnapshot();
            if (index >= model.entries.size() || !model.entries[index].canHide) return false;
            confirm(i18n::tr(i18n::TextId::HideCatalogConfirm), [this, index] { controller.hide(index); pushOperation(controller); });
            return true;
        }, false, false, brls::SOUND_CLICK);
        // TabFrame also has a B action that focuses its sidebar. Registering
        // this on the recycler gives folder navigation priority while a row is
        // focused, then lets TabFrame handle B only at the root folder.
        recycler->registerAction(i18n::tr(i18n::TextId::Back), brls::BUTTON_B, [this](brls::View*) {
            if (controller.filesSnapshot().breadcrumb.empty()) return false;
            controller.backFolder();
            return true;
        }, false, false, brls::SOUND_BACK);
        refresh();
        if (controller.filesSnapshot().entries.empty()) controller.refreshFiles();
    }

    void refresh() override {
        const auto model = controller.filesSnapshot();
        recycler->reloadData();
        updateActions(focusedRow());
    }

  private:
    brls::RecyclerFrame* recycler{};

    void updateActions(size_t index) {
        const auto model = controller.filesSnapshot();
        const bool valid = index < model.entries.size();
        recycler->setActionAvailable(brls::BUTTON_X, !controller.providersSnapshot().empty());
        recycler->setActionAvailable(brls::BUTTON_Y, model.canChangeScope);
        recycler->setActionAvailable(brls::BUTTON_LT, valid && model.entries[index].canHide);
    }

    std::vector<Row> rows() const {
        const auto model = controller.filesSnapshot();
        std::vector<Row> values;
        for (const auto& entry : model.entries) values.emplace_back(entry.title, entry.detail);
        if (model.loading) values.emplace_back(i18n::tr(i18n::TextId::LoadingFiles), "");
        else if (!model.error.empty()) values.emplace_back(model.error, "");
        else if (values.empty()) values.emplace_back(i18n::tr(i18n::TextId::EmptyFolder), "");
        return values;
    }

    void chooseProvider() {
        const auto providers = controller.providersSnapshot();
        if (providers.empty()) return;
        std::vector<std::string> names;
        int selected{};
        const auto active = controller.filesSnapshot().providerId;
        for (size_t index = 0; index < providers.size(); ++index) {
            names.push_back(providers[index].name);
            if (providers[index].id == active) selected = static_cast<int>(index);
        }
        auto* selector = new brls::Dropdown(i18n::tr(i18n::TextId::StorageProviders), names,
            [this, providers](int index) {
                if (index >= 0 && static_cast<size_t>(index) < providers.size())
                    controller.selectProvider(providers[static_cast<size_t>(index)].id);
            }, selected);
        brls::Application::pushActivity(new brls::Activity(selector));
    }

    void chooseScope() {
        const auto model = controller.filesSnapshot();
        if (!model.canChangeScope) return;
        auto* selector = new brls::Dropdown(i18n::tr(i18n::TextId::Files),
            {i18n::tr(i18n::TextId::MyDrive), i18n::tr(i18n::TextId::SharedWithMe)},
            [this](int index) { controller.setSharedWithMe(index == 1); }, model.shared ? 1 : 0);
        brls::Application::pushActivity(new brls::Activity(selector));
    }

    void select(size_t index) {
        const auto model = controller.filesSnapshot();
        if (index >= model.entries.size()) return;
        const auto& entry = model.entries[index];
        if (entry.folder) {
            controller.openFolder(index);
            return;
        }
        if (!entry.canDownload) return;
        auto* dialog = new brls::Dialog(entry.title);
        dialog->addButton(i18n::tr(i18n::TextId::Download), [this, index] {
            chooseResume(index, false, NspInstallStorage::SdCard);
        });
        if (entry.installable && !controller.appletMode())
            dialog->addButton(i18n::tr(i18n::TextId::DownloadAndInstall), [this, index] {
                chooseInstallDestination(index);
            });
        dialog->addButton(i18n::tr(i18n::TextId::Cancel), [] {});
        dialog->open();
    }

    void chooseInstallDestination(size_t index) {
        auto* dialog = new brls::Dialog(i18n::tr(i18n::TextId::DestinationHint));
        dialog->addButton(i18n::tr(i18n::TextId::SdCard), [this, index] { chooseResume(index, true, NspInstallStorage::SdCard); });
        dialog->addButton(i18n::tr(i18n::TextId::InternalStorage), [this, index] { chooseResume(index, true, NspInstallStorage::InternalUser); });
        dialog->addButton(i18n::tr(i18n::TextId::Cancel), [] {});
        dialog->open();
    }

    void chooseResume(size_t index, bool installAfter, NspInstallStorage destination) {
        const auto model = controller.filesSnapshot();
        if (index >= model.entries.size()) return;
        const auto& entry = model.entries[index];
        auto start = [this, index, installAfter, destination](bool restart) {
            controller.download(index, installAfter, destination, restart);
            pushOperation(controller);
        };
        if (!entry.partial) { start(false); return; }
        auto* dialog = new brls::Dialog(i18n::tr(entry.restartRequired
            ? i18n::TextId::RemoteChanged : i18n::TextId::PartialDownloadFound));
        if (!entry.restartRequired)
            dialog->addButton(i18n::tr(i18n::TextId::Resume), [start] { start(false); });
        dialog->addButton(i18n::tr(i18n::TextId::Restart), [start] { start(true); });
        dialog->addButton(i18n::tr(i18n::TextId::Cancel), [] {});
        dialog->open();
    }
};

class LibraryView final : public ObservedBox {
  public:
    explicit LibraryView(AppController& appController) : ObservedBox(appController) {
        setPadding(24, 48, 24, 48);
        warning = label("", 18, 42);
        warning->setTextColor(nvgRGB(230, 140, 30));
        recycler = new brls::RecyclerFrame();
        recycler->setGrow(1);
        recycler->registerCell("detail", [] { return new FocusDetailCell(); });
        recycler->setDataSource(new ModelDataSource([this] { return rows(); }, [this](size_t index) { install(index); }, {},
            [this](size_t index) { updateActions(index); }));
        addView(warning);
        addView(recycler);
        recycler->registerAction(i18n::tr(i18n::TextId::DeleteDownload), brls::BUTTON_Y, [this](brls::View*) {
            const auto index = focusedRow();
            const auto model = controller.librarySnapshot();
            if (index >= model.entries.size() || !model.entries[index].canRemovePackage) return false;
            confirm(i18n::tr(i18n::TextId::DeleteDownloadWarning), [this, index] { controller.removeLibraryPackage(index); });
            return true;
        }, false, false, brls::SOUND_CLICK);
        recycler->registerAction(i18n::tr(i18n::TextId::RemoveNsp), brls::BUTTON_X, [this](brls::View*) {
            const auto index = focusedRow();
            const auto model = controller.librarySnapshot();
            if (index >= model.entries.size() || !model.entries[index].canUninstall) return false;
            confirm(i18n::tr(i18n::TextId::BaseRemovalWarning), [this, index] { controller.uninstallLibraryItem(index); pushOperation(controller); });
            return true;
        }, false, false, brls::SOUND_CLICK);
        refresh();
    }

    void refresh() override {
        const auto model = controller.librarySnapshot();
        warning->setText(model.appletMode ? i18n::tr(i18n::TextId::AppletModeWarning) : "");
        warning->setVisibility(model.appletMode ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        recycler->reloadData();
        updateActions(focusedRow());
    }

  private:
    brls::Label* warning{};
    brls::RecyclerFrame* recycler{};

    void updateActions(size_t index) {
        const auto model = controller.librarySnapshot();
        const bool valid = index < model.entries.size();
        recycler->setActionAvailable(brls::BUTTON_Y, valid && model.entries[index].canRemovePackage);
        recycler->setActionAvailable(brls::BUTTON_X, valid && model.entries[index].canUninstall);
    }

    std::vector<Row> rows() const {
        const auto model = controller.librarySnapshot();
        std::vector<Row> values;
        for (const auto& entry : model.entries) values.emplace_back(entry.title, entry.detail);
        if (values.empty()) values.emplace_back(i18n::tr(i18n::TextId::NoIndexedDownloads), "");
        return values;
    }

    void install(size_t index) {
        const auto model = controller.librarySnapshot();
        if (index >= model.entries.size() || !model.entries[index].canInstall) return;
        auto* dialog = new brls::Dialog(i18n::tr(i18n::TextId::DestinationHint));
        dialog->addButton(i18n::tr(i18n::TextId::SdCard), [this, index] { controller.installLibraryItem(index, NspInstallStorage::SdCard); pushOperation(controller); });
        dialog->addButton(i18n::tr(i18n::TextId::InternalStorage), [this, index] { controller.installLibraryItem(index, NspInstallStorage::InternalUser); pushOperation(controller); });
        dialog->addButton(i18n::tr(i18n::TextId::Cancel), [] {});
        dialog->open();
    }
};

class SettingsView final : public ObservedBox {
  public:
    explicit SettingsView(AppController& appController) : ObservedBox(appController) {
        setPadding(24, 48, 24, 48);
        cleanup = new brls::BooleanCell();
        cleanup->init(i18n::tr(i18n::TextId::AutoCleanup), controller.settingsSnapshot().deleteAfterInstall,
            [this](bool enabled) { controller.setDeleteAfterInstall(enabled); });
        account = new brls::DetailCell();
        account->setText(i18n::tr(i18n::TextId::ConnectDrive));
        account->registerClickAction([this](brls::View*) { pushPairing(controller); return true; });
        language = new brls::SelectorCell();
        const auto current = i18n::parseLanguage(controller.settingsSnapshot().languageCode);
        language->init(i18n::tr(i18n::TextId::Language), {
            std::string(i18n::languageName(i18n::Language::EnUs)),
            std::string(i18n::languageName(i18n::Language::PtBr)),
            std::string(i18n::languageName(i18n::Language::EsEs)),
        }, static_cast<int>(current), [this](int selected) {
            controller.setLanguage(static_cast<i18n::Language>(selected));
            brls::Application::notify(i18n::tr(i18n::TextId::RestartRequired));
        });
        homeStorage = new brls::DetailCell();
        homeStorage->setText(i18n::tr(i18n::TextId::HomeStorage));
        homeStorage->registerClickAction([this](brls::View*) { pushHomeStorage(controller); return true; });
        addView(cleanup);
        addView(account);
        addView(language);
        addView(homeStorage);
        addView(new brls::Padding());
        refresh();
    }

    void refresh() override {
        const auto model = controller.settingsSnapshot();
        cleanup->setOn(model.deleteAfterInstall, false);
        account->setDetailText(model.account);
        homeStorage->setDetailText(model.homeStorage);
    }

  private:
    brls::BooleanCell* cleanup{};
    brls::DetailCell* account{};
    brls::SelectorCell* language{};
    brls::DetailCell* homeStorage{};
};

class MainActivity final : public brls::Activity {
  public:
    explicit MainActivity(AppController& controller) : controller(controller) {}

    brls::View* createContentView() override {
        tabs = new brls::TabFrame();
        auto navigate = [this](int page) { current = page; tabs->focusTab(page); };
        tabs->addTab(i18n::tr(i18n::TextId::Home), [this, navigate] { current = 0; return new HomeView(controller, navigate); });
        tabs->addTab(i18n::tr(i18n::TextId::Files), [this] { current = 1; return new FilesView(controller); });
        tabs->addTab(i18n::tr(i18n::TextId::Library), [this] { current = 2; return new LibraryView(controller); });
        tabs->addTab(i18n::tr(i18n::TextId::Settings), [this] { current = 3; return new SettingsView(controller); });
        auto* frame = new brls::AppletFrame(tabs);
        frame->setTitle(i18n::tr(i18n::TextId::AppName));
        return frame;
    }

    void onContentAvailable() override {
        registerAction(i18n::tr(i18n::TextId::ButtonL), brls::BUTTON_LB, [this](brls::View*) { current = (current + 3) % 4; tabs->focusTab(current); return true; }, true, false, brls::SOUND_FOCUS_CHANGE);
        registerAction(i18n::tr(i18n::TextId::ButtonR), brls::BUTTON_RB, [this](brls::View*) { current = (current + 1) % 4; tabs->focusTab(current); return true; }, true, false, brls::SOUND_FOCUS_CHANGE);
        registerAction(i18n::tr(i18n::TextId::Exit), brls::BUTTON_START, [this](brls::View*) {
            const auto operation = controller.operationSnapshot();
            confirm(i18n::tr(operation.busy ? i18n::TextId::ExitActiveConfirm : i18n::TextId::ExitConfirm), [this] {
                controller.cancelOperation();
                brls::Application::quit();
            });
            return true;
        }, false, false, brls::SOUND_CLICK);
    }

  private:
    AppController& controller;
    brls::TabFrame* tabs{};
    int current{};
};

} // namespace

struct Ui::Impl { bool initialized{}; };

Ui::Ui() : impl_(std::make_unique<Impl>()) {}
Ui::~Ui() = default;

bool Ui::initialize(const std::string& locale, std::string& error) {
    brls::Platform::APP_LOCALE_DEFAULT = locale;
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);
    brls::Logger::info("ui: Application::init begin");
    if (!brls::Application::init()) {
        brls::Logger::error("ui: Application::init failed");
        error = i18n::tr(i18n::TextId::StartFailed);
        return false;
    }
    brls::Logger::info("ui: Application::init complete");
    brls::Logger::info("ui: createWindow begin");
    brls::Application::createWindow(i18n::tr(i18n::TextId::AppName));
    brls::Logger::info("ui: createWindow complete");
    brls::Application::setGlobalQuit(false);
    brls::Theme::getLightTheme().addColor("switchdrive/accent", nvgRGB(0, 180, 205));
    brls::Theme::getDarkTheme().addColor("switchdrive/accent", nvgRGB(45, 205, 225));
    impl_->initialized = true;
    return true;
}

int Ui::run(AppController& controller) {
    if (!impl_->initialized) return 1;
    brls::Logger::info("ui: pushing main activity");
    brls::Application::pushActivity(new MainActivity(controller));
    brls::Logger::info("ui: entering main loop");
    while (brls::Application::mainLoop()) {}
    brls::Logger::info("ui: main loop exited");
    return 0;
}

void Ui::shutdown() { impl_->initialized = false; }

} // namespace switchdrive::ui
