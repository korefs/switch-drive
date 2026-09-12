// Render the production UI on a host for visual review. The system font used
// on Switch is supplied through pl:u; host previews use the font named in
// SWITCHDRIVE_PREVIEW_FONT and intentionally do not emulate Switch services.
#include "switchdrive/ui.hpp"
#include "switchdrive/i18n.hpp"
#include <filesystem>
#include <iostream>

using namespace switchdrive;
using namespace switchdrive::i18n;

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "build/ui-preview";
    std::filesystem::create_directories(output);
    ui::Ui view;
    std::string error;
    if (!view.initialize(error)) { std::cerr << error << '\n'; return 1; }
    for (const auto language : {Language::EnUs, Language::PtBr, Language::EsEs}) {
        setLanguage(language);
        const std::string code(languageCode(language));
        view.setBrand(tr(TextId::AppName));
        const std::vector<std::string> tabs{tr(TextId::Home), tr(TextId::Files), tr(TextId::Library), tr(TextId::Settings)};
        for (const bool applet : {false, true}) {
            const auto save = [&](const char* name) { return view.savePreview((output / (code + (applet ? "-applet-" : "-application-") + name + ".bmp")).string(), applet); };
            view.clear();
            view.setAppletWarning(tr(TextId::AppletModeWarning));
            view.setHeader(tr(TextId::Home), tabs, 0);
            view.setSubtitle(tr(TextId::HomeSubtitle));
            view.setHint(tr(TextId::NavigationHint));
            view.setCards({{tr(TextId::ConnectDrive), tr(TextId::NoAccountConnected), 1, ui::Icon::Cloud}, {tr(TextId::Files), tr(TextId::MyDrive), 4, ui::Icon::Folder}, {tr(TextId::Library), tr(TextId::LibrarySubtitle), 8, ui::Icon::Library}});
            if (!save("home")) return 1;
            view.moveFocus(ui::Direction::Right);
            if (!save("home-focus-files")) return 1;
            view.moveFocus(ui::Direction::Down);
            if (!save("home-focus-library")) return 1;
            view.moveFocus(ui::Direction::Left);
            if (!save("home-focus-menu")) return 1;
            view.clear();
            view.setHeader(tr(TextId::Settings), tabs, 3);
            view.setSubtitle(tr(TextId::SettingsSubtitle));
            view.setHint(tr(TextId::NavigationHint));
            view.setCards({{tr(TextId::AutoCleanup), tr(TextId::Yes), 1, ui::Icon::Settings}, {tr(TextId::ConnectDrive), tr(TextId::NoAccountConnected), 4, ui::Icon::Cloud}, {tr(TextId::Language), std::string(languageName(language)), 8, ui::Icon::Language}});
            if (!save("settings")) return 1;
            view.clear();
            view.setHeader(tr(TextId::MyDrive));
            view.setSubtitle("conta@example.com");
            view.setHint(tr(TextId::BrowseHint));
            view.setRows({{"Homebrew", tr(TextId::Folder), ui::Icon::Folder}, {"Capturas de tela", tr(TextId::Folder), ui::Icon::Folder}, {"switch-drive.nro", "3.4 MiB"}, {"Um arquivo com nome muito longo para verificar os limites e a legibilidade da interface.zip", "4096.0 MiB"}, {"Notas.txt", "0.1 MiB"}, {"Fotos.zip", "350.0 MiB"}, {"Backup.zip", "1800.0 MiB"}}, 3);
            if (!save("files")) return 1;
        }
    }
    return 0;
}
