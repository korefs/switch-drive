#include "switchdrive/app_controller.hpp"
#include "switchdrive/core.hpp"
#include "switchdrive/i18n.hpp"
#include "switchdrive/ui.hpp"

#include <borealis.hpp>
#include <curl/curl.h>
#include <switch.h>

#include <cstdio>
#include <exception>
#include <string>
#include <sys/stat.h>

using namespace switchdrive;

namespace {
constexpr const char* kRoot = "sdmc:/switch-drive";
constexpr const char* kBootLog = "sdmc:/switch-drive/boot.log";

FILE* openBootLog() {
    mkdir(kRoot, 0777);
    FILE* output = std::fopen(kBootLog, "w");
    if (output) std::setvbuf(output, nullptr, _IONBF, 0);
    return output;
}

void logBoot(FILE* output, const char* message) {
    if (output) std::fprintf(output, "%s\n", message);
}

void closeBootLog(FILE* output) {
    if (!output) return;
    brls::Logger::setLogOutput(stdout);
    std::fclose(output);
}

int consoleFallback(const std::string& error) {
    consoleInit(nullptr);
    PadState pad;
    padInitializeDefault(&pad);
    std::printf(i18n::tr(i18n::TextId::GraphicsUnavailable), error.c_str());
    std::printf("\n%s\n", i18n::tr(i18n::TextId::Continue));
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_A) break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
    return 1;
}
} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    FILE* bootLog = openBootLog();
    brls::Logger::setLogOutput(bootLog ? bootLog : stdout);
    brls::Logger::setThreadSafeLogging(true);
    logBoot(bootLog, "0.2.6-borealis: main entered");
    if (bootLog)
        std::fprintf(bootLog, "environment: applet_type=%u\n", static_cast<unsigned>(appletGetAppletType()));

    ui::Ui application;
    bool curlReady = false;
    try {
        const State initial = StateStore(kRoot).load();
        i18n::setLanguage(i18n::parseLanguage(initial.language));
        logBoot(bootLog, "main: state loaded");

        std::string graphicsError;
        logBoot(bootLog, "main: initializing Borealis");
        if (!application.initialize(std::string(i18n::languageCode(i18n::currentLanguage())), graphicsError)) {
            logBoot(bootLog, "main: Borealis initialization failed");
            closeBootLog(bootLog);
            return consoleFallback(graphicsError);
        }
        logBoot(bootLog, "main: Borealis ready");

        // The Borealis Switch wrapper initializes libnx sockets before main().
        // Initializing them again here can fail or unbalance socketExit().
        const CURLcode curlResult = curl_global_init(CURL_GLOBAL_DEFAULT);
        curlReady = curlResult == CURLE_OK;
        if (bootLog) std::fprintf(bootLog, "main: curl=%u\n", static_cast<unsigned>(curlResult));

        const bool appletMode = appletGetAppletType() != AppletType_Application;
        int result{};
        {
            AppController controller(kRoot, appletMode);
            controller.setNetworkStatus(curlReady, static_cast<uint32_t>(curlResult));
            controller.recover();
            logBoot(bootLog, "main: recovery complete");
            result = application.run(controller);
        }

        logBoot(bootLog, "main: application loop finished");
        if (curlReady) curl_global_cleanup();
        application.shutdown();
        logBoot(bootLog, "main: clean shutdown");
        closeBootLog(bootLog);
        return result;
    } catch (const std::exception& exception) {
        if (bootLog) std::fprintf(bootLog, "main: unhandled exception: %s\n", exception.what());
    } catch (...) {
        logBoot(bootLog, "main: unhandled non-standard exception");
    }

    if (curlReady) curl_global_cleanup();
    application.shutdown();
    closeBootLog(bootLog);
    return 1;
}
