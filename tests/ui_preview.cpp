#include "switchdrive/i18n.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace switchdrive::i18n;

namespace {
std::string escape(std::string value) {
    size_t at{};
    while ((at = value.find('&', at)) != std::string::npos) { value.replace(at, 1, "&amp;"); at += 5; }
    at = 0;
    while ((at = value.find('<', at)) != std::string::npos) { value.replace(at, 1, "&lt;"); at += 4; }
    return value;
}

void screen(std::ofstream& output, const std::string& theme, const std::string& language) {
    output << "<section class='screen " << theme << "'><header>Switch Drive <span>12:34 &nbsp; Wi-Fi &nbsp; 86%</span></header>"
        "<aside><b>" << escape(tr(TextId::Home)) << "</b><b>" << escape(tr(TextId::Files)) << "</b><b>"
        << escape(tr(TextId::Library)) << "</b><b>" << escape(tr(TextId::Settings)) << "</b></aside>"
        "<main><h1>" << escape(tr(TextId::Files)) << "</h1><div class='selector'>" << escape(tr(TextId::StorageProviders))
        << "<span>Home Storage</span></div><div class='selector'>" << escape(tr(TextId::Files)) << "<span>"
        << escape(tr(TextId::MyDrive)) << "</span></div><p class='path'>Home Storage / Downloads / " << language << "</p>"
        "<div class='row focused'><i>\u25a0</i><b>Homebrew</b><span>" << escape(tr(TextId::Folder)) << "</span></div>"
        "<div class='row'><i>\u25a1</i><b>Um arquivo com nome muito longo para revisar truncamento e foco animado.nsz</b><span>8.4 GiB</span></div>"
        "<div class='row'><i>\u25a1</i><b>Switch Drive.nsp</b><span>128.0 MiB</span></div>"
        "<div class='progress'><em></em></div><p>3.1 GiB / 8.4 GiB \u00b7 36.9% \u00b7 18.2 MiB/s \u00b7 ETA 4m 52s</p></main>"
        "<footer><span>L/R " << escape(tr(TextId::NavigationHint)) << "</span><span>X " << escape(tr(TextId::Download))
        << " &nbsp; Y " << escape(tr(TextId::DownloadAndInstall)) << "</span></footer></section>";
}

void pairingScreen(std::ofstream& output, const std::string& theme) {
    output << "<section class='screen " << theme << "'><header>Switch Drive <span>12:34 &nbsp; Wi-Fi &nbsp; 86%</span></header>"
        "<main class='full centered'><h1>" << escape(tr(TextId::ConnectDrive)) << "</h1><p>"
        << escape(tr(TextId::ScanWithPhone)) << "</p><div class='qr'></div><strong>SWDR-4821</strong>"
        "<p>https://switch-drive.example/pair</p><button class='pairing-action'>" << escape(tr(TextId::CheckNow))
        << "</button></main><footer><span>B " << escape(tr(TextId::Cancel)) << "</span></footer></section>";
}

void dialogScreen(std::ofstream& output, const std::string& theme) {
    output << "<section class='screen " << theme << "'><header>Switch Drive <span>12:34 &nbsp; Wi-Fi &nbsp; 86%</span></header>"
        "<aside><b>" << escape(tr(TextId::Home)) << "</b><b>" << escape(tr(TextId::Files)) << "</b><b>"
        << escape(tr(TextId::Library)) << "</b><b>" << escape(tr(TextId::Settings)) << "</b></aside>"
        "<main><h1>" << escape(tr(TextId::Library)) << "</h1><div class='row'><b>Switch Drive.nsp</b><span>128.0 MiB</span></div></main>"
        "<div class='shade'></div><div class='dialog'><h2>" << escape(tr(TextId::DestinationHint)) << "</h2><button>"
        << escape(tr(TextId::SdCard)) << "</button><button>" << escape(tr(TextId::InternalStorage))
        << "</button><button>" << escape(tr(TextId::Cancel)) << "</button></div></section>";
}
} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path outputPath = argc > 1 ? argv[1] : "build/ui-preview.html";
    std::filesystem::create_directories(outputPath.parent_path());
    std::ofstream output(outputPath);
    if (!output) return 1;
    output << R"(<!doctype html><meta charset='utf-8'><title>Switch Drive UI preview</title><style>
body{margin:0;background:#202124;font-family:Arial,sans-serif}.screen{position:relative;width:1280px;height:720px;margin:24px auto;overflow:hidden;background:#f6f6f7;color:#222}.dark{background:#202124;color:#f4f4f4}.screen header{height:72px;box-sizing:border-box;padding:24px 42px;border-bottom:1px solid #bbb;font-size:25px}.screen header span{float:right;font-size:16px}.screen aside{position:absolute;top:73px;bottom:54px;width:260px;padding-top:34px;border-right:1px solid #aaa}.screen aside b{display:block;padding:22px 34px}.screen aside b:nth-child(2){border-left:7px solid #00b4cd;background:#00b4cd22}.screen main{position:absolute;left:261px;right:0;top:73px;bottom:54px;padding:22px 42px}.screen main.full{left:0}.screen main.centered{text-align:center}.screen h1{font-size:30px;margin:0 0 12px}.selector,.row{height:52px;box-sizing:border-box;padding:16px 20px;border-bottom:1px solid #aaa}.selector span,.row span{float:right}.path{opacity:.65}.row i{color:#00b4cd;margin-right:18px}.row.focused{outline:3px solid #00b4cd;border-radius:5px}.progress{height:10px;margin-top:24px;background:#9995;border-radius:5px}.progress em{display:block;width:37%;height:100%;background:#00b4cd;border-radius:5px}.qr{width:260px;height:260px;margin:8px auto;background:repeating-conic-gradient(#111 0 25%,#fff 0 50%) 0/24px 24px;border:18px solid #fff}.centered strong{font-size:30px}.pairing-action{display:block;width:360px;padding:15px;margin:8px auto 0;border:0;border-radius:5px;background:#00b4cd;color:#fff;font-size:18px;outline:3px solid #00b4cd88}.screen footer{position:absolute;bottom:0;left:0;right:0;height:54px;box-sizing:border-box;padding:18px 42px;border-top:1px solid #aaa}.screen footer span:last-child{float:right}.shade{position:absolute;inset:0;background:#0008}.dialog{position:absolute;left:360px;right:360px;top:190px;padding:34px;background:#f8f8f8;color:#222;border-radius:9px;box-shadow:0 10px 40px #0009}.dark .dialog{background:#303034;color:#fff}.dialog button{display:block;width:100%;padding:16px;margin-top:12px;border:0;border-radius:5px;font-size:18px}.dialog button:first-of-type{outline:3px solid #00b4cd}.dark header,.dark aside,.dark footer,.dark .selector,.dark .row{border-color:#555}
</style>)";
    for (const auto language : {Language::EnUs, Language::PtBr, Language::EsEs}) {
        setLanguage(language);
        screen(output, "light", std::string(languageCode(language)));
        screen(output, "dark", std::string(languageCode(language)));
        pairingScreen(output, "light");
        pairingScreen(output, "dark");
        dialogScreen(output, "light");
        dialogScreen(output, "dark");
    }
    std::cout << outputPath.string() << '\n';
}
