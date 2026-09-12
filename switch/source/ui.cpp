#include "switchdrive/ui.hpp"
#include "switchdrive/ui_model.hpp"
#include "switchdrive/i18n.hpp"

#ifdef __SWITCH__
#include <switch.h>
#else
#include <cstdlib>
constexpr uint64_t HidNpadButton_A = 1, HidNpadButton_X = 4;
#endif

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unordered_map>

namespace switchdrive::ui {
namespace {

constexpr SDL_Color kBackground{0x07, 0x11, 0x1F, 0xFF};
constexpr SDL_Color kSurface{0x0D, 0x1B, 0x2E, 0xFF};
constexpr SDL_Color kRaised{0x13, 0x26, 0x3D, 0xFF};
constexpr SDL_Color kSelected{0x13, 0x3B, 0x51, 0xFF};
constexpr SDL_Color kWarningSurface{0x3A, 0x35, 0x21, 0xFF};
constexpr SDL_Color kAccent{0x25, 0xDD, 0xF4, 0xFF};
constexpr SDL_Color kText{0xF5, 0xFA, 0xFF, 0xFF};
constexpr SDL_Color kMuted{0x91, 0xA4, 0xBC, 0xFF};
constexpr SDL_Color kWarning{0xF6, 0xC8, 0x5F, 0xFF};
constexpr int kWidth = 1280;
constexpr int kHeight = 720;
[[maybe_unused]] constexpr const char* kBootLog = "sdmc:/switch-drive/boot.log";

void appendDiagnostic(const char* stage, bool reset = false) {
    #ifdef __SWITCH__
    mkdir("sdmc:/switch-drive", 0777);
    std::FILE* output = std::fopen(kBootLog, reset ? "w" : "a");
    if (!output) return;
    std::fprintf(output, "%s\n", stage);
    std::fflush(output);
    std::fclose(output);
    #else
    (void)stage; (void)reset;
    #endif
}

std::string stripAnsi(const std::string& value) {
    std::string output;
    for (size_t index = 0; index < value.size();) {
        if (value[index] == '\x1b' && index + 1 < value.size() && value[index + 1] == '[') {
            index += 2;
            while (index < value.size() && ((value[index] >= '0' && value[index] <= '9') || value[index] == ';')) ++index;
            if (index < value.size()) ++index;
        } else {
            output += value[index++];
        }
    }
    return output;
}

void fillRect(SDL_Surface* surface, int x, int y, int width, int height, SDL_Color color) {
    SDL_Rect rect{x, y, width, height};
    SDL_FillRect(surface, &rect, SDL_MapRGBA(surface->format, color.r, color.g, color.b, color.a));
}

void roundedRect(SDL_Surface* surface, int x, int y, int width, int height, int radius, SDL_Color color) {
    radius = std::min(radius, std::min(width, height) / 2);
    fillRect(surface, x + radius, y, width - radius * 2, height, color);
    fillRect(surface, x, y + radius, width, height - radius * 2, color);
    for (int row = 0; row < radius; ++row) {
        const float dy = static_cast<float>(radius - row) - 0.5f;
        const int inset = static_cast<int>(static_cast<float>(radius) - std::sqrt(std::max(0.0f, static_cast<float>(radius * radius) - dy * dy)));
        fillRect(surface, x + inset, y + row, width - inset * 2, 1, color);
        fillRect(surface, x + inset, y + height - row - 1, width - inset * 2, 1, color);
    }
}

void drawIcon(SDL_Surface* surface, Icon icon, int x, int y, SDL_Color color) {
    if (icon == Icon::Folder || icon == Icon::Library) {
        roundedRect(surface, x + 2, y + 6, 22, 12, 4, color);
        roundedRect(surface, x + 2, y + 14, 46, 30, 5, color);
        fillRect(surface, x + 10, y + 24, 30, 3, kSurface);
    } else if (icon == Icon::Cloud) {
        roundedRect(surface, x + 9, y + 6, 28, 28, 14, color);
        roundedRect(surface, x, y + 20, 50, 22, 11, color);
        fillRect(surface, x + 23, y + 23, 4, 13, kSurface);
    } else if (icon == Icon::Settings || icon == Icon::Language) {
        for (int row = 0; row < 3; ++row) {
            roundedRect(surface, x + 4, y + 9 + row * 13, 40, 3, 1, color);
            roundedRect(surface, x + (row == 1 ? 28 : 12), y + 5 + row * 13, 10, 11, 4, color);
        }
    } else {
        roundedRect(surface, x + 8, y + 2, 32, 44, 5, color);
        for (int row = 0; row < 3; ++row) fillRect(surface, x + 15, y + 14 + row * 8, 18, 3, kSurface);
    }
}

} // namespace

struct Ui::Impl {
    struct TextSurface { SDL_Surface* surface{}; int width{}; int height{}; uint64_t used{}; };
    SDL_Surface* screen{};
    SDL_Surface* logo{};
    #ifdef __SWITCH__
    Framebuffer framebuffer{};
    PadState pad{};
    PrintConsole* console{};
    #endif
    bool framebufferReady{};
    std::array<TTF_Font*, 3> fonts{};
    bool ready{};
    bool fallback{};
    bool plReady{};
    bool romfsReady{};
    bool ttfReady{};
    bool touchDown{};
    int lastTouchY{};
    int touchStartX{};
    int touchStartY{};
    bool dragged{};
    bool applet{};
    size_t textCacheLimit{96};
    uint64_t pressed{};
    int tabSelection{-1};
    std::string brand;
    std::string header;
    std::vector<std::string> tabs;
    int activeTab{-1};
    std::vector<std::string> lines;
    std::string current;
    std::string hint;
    std::vector<Card> cards;
    MenuFocus focus;
    DirectionRepeat directionRepeat;
    uint64_t cardAction{};
    std::vector<Row> rows;
    size_t selectedRow{};
    int rowSelection{-1};
    std::string subtitle;
    std::string appletWarning;
    uint64_t progressCurrent{};
    uint64_t progressTotal{};
    std::unordered_map<std::string, TextSurface> textCache;
    uint64_t frame{};

    ~Impl() {
        for (auto& [_, item] : textCache) if (item.surface) SDL_FreeSurface(item.surface);
        for (TTF_Font* font : fonts) if (font) TTF_CloseFont(font);
        if (logo) SDL_FreeSurface(logo);
        if (screen) SDL_FreeSurface(screen);
        #ifdef __SWITCH__
        if (framebufferReady) framebufferClose(&framebuffer);
        #endif
    }

    TextSurface text(const std::string& value, int fontIndex, SDL_Color color, unsigned width) {
        const std::string key = std::to_string(fontIndex) + ':' + std::to_string(width) + ':' + std::to_string(color.r) + ':' + std::to_string(color.g) + ':' + std::to_string(color.b) + ':' + value;
        const auto found = textCache.find(key);
        if (found != textCache.end()) { found->second.used = frame; return found->second; }
        SDL_Surface* rendered = width ? TTF_RenderUTF8_Blended_Wrapped(fonts[fontIndex], value.c_str(), color, width) : TTF_RenderUTF8_Blended(fonts[fontIndex], value.c_str(), color);
        if (!rendered) return {};
        TextSurface output{rendered, rendered->w, rendered->h, frame};
        size_t cacheBytes = static_cast<size_t>(rendered->pitch) * rendered->h;
        for (const auto& [_, item] : textCache) cacheBytes += static_cast<size_t>(item.surface->pitch) * item.surface->h;
        const size_t cacheBudget = (applet ? 4 : 12) * 1024 * 1024;
        while (!textCache.empty() && (textCache.size() >= textCacheLimit || cacheBytes > cacheBudget)) {
            auto oldest = std::min_element(textCache.begin(), textCache.end(), [](const auto& left, const auto& right) { return left.second.used < right.second.used; });
            if (oldest != textCache.end()) { cacheBytes -= static_cast<size_t>(oldest->second.surface->pitch) * oldest->second.surface->h; SDL_FreeSurface(oldest->second.surface); textCache.erase(oldest); }
        }
        textCache.emplace(key, output);
        return output;
    }

    void drawText(const std::string& value, float x, float y, int fontIndex, SDL_Color color, unsigned width = 0) {
        if (value.empty()) return;
        std::string bounded = value.substr(0, 1024);
        if (value.size() > bounded.size()) {
            while (!bounded.empty() && (static_cast<unsigned char>(value[bounded.size()]) & 0xc0) == 0x80) bounded.pop_back();
        }
        if (!width) {
            SDL_Rect clip{};
            SDL_GetClipRect(screen, &clip);
            const int limit = std::max(1, clip.x + clip.w - static_cast<int>(x));
            int measured{};
            TTF_SizeUTF8(fonts[fontIndex], bounded.c_str(), &measured, nullptr);
            if (measured > limit) {
                const std::string suffix = i18n::tr(i18n::TextId::Ellipsis);
                do {
                    if (bounded.empty()) break;
                    bounded.pop_back();
                    while (!bounded.empty() && (static_cast<unsigned char>(bounded.back()) & 0xc0) == 0x80) bounded.pop_back();
                    // Remove the leading byte of the truncated multibyte glyph.
                    if (!bounded.empty() && static_cast<unsigned char>(bounded.back()) >= 0xc0) bounded.pop_back();
                    TTF_SizeUTF8(fonts[fontIndex], (bounded + suffix).c_str(), &measured, nullptr);
                } while (measured > limit);
                bounded += suffix;
            }
        }
        const TextSurface item = text(bounded, fontIndex, color, width);
        if (!item.surface || !screen) return;
        SDL_Rect target{static_cast<int>(x), static_cast<int>(y), item.width, item.height};
        SDL_BlitSurface(item.surface, nullptr, screen, &target);
    }
};

Ui::Ui() : impl_(std::make_unique<Impl>()) {}
Ui::~Ui() { shutdown(); }

bool Ui::initialize(std::string& error) {
    auto& data = *impl_;
    if (data.ready) return true;
    appendDiagnostic("0.2.2: main entered", true);
    #ifdef __SWITCH__
    // Launch mode comes from the homebrew ABI, not the launcher name.
    // Sphaira can launch us in either application or library-applet mode.
    data.applet = appletGetAppletType() != AppletType_Application;
    data.textCacheLimit = data.applet ? 24 : 96;
    {
        char environment[128]{};
        std::snprintf(environment, sizeof(environment), "environment: applet_type=%d heap_override=%d heap_size=%llu", static_cast<int>(appletGetAppletType()), envHasHeapOverride() ? 1 : 0, static_cast<unsigned long long>(envHasHeapOverride() ? envGetHeapOverrideSize() : 0));
        appendDiagnostic(environment);
    }
    appendDiagnostic("ui: pl initialize");
    if (R_FAILED(plInitialize(PlServiceType_User))) { error = "pl:u"; return false; }
    data.plReady = true;
    appendDiagnostic("ui: romfs initialize");
    const Result romfsResult = romfsInit();
    data.romfsReady = R_SUCCEEDED(romfsResult);
    if (!data.romfsReady) appendDiagnostic("ui: optional RomFS unavailable; using drawn logo");
    #endif
    appendDiagnostic("ui: SDL_ttf initialize");
    if (TTF_Init() != 0) { error = TTF_GetError(); shutdown(); return false; }
    data.ttfReady = true;
    #ifdef __SWITCH__
    PlFontData font{};
    appendDiagnostic("ui: shared font acquire");
    if (R_FAILED(plGetSharedFontByType(&font, PlSharedFontType_Standard))) { error = "shared font"; shutdown(); return false; }
    #endif
    for (size_t index = 0; index < data.fonts.size(); ++index) {
        const int pointSize = index == 0 ? 22 : index == 1 ? 30 : 42;
        #ifdef __SWITCH__
        data.fonts[index] = TTF_OpenFontRW(SDL_RWFromConstMem(font.address, font.size), 1, pointSize);
        #else
        const char* fontPath = std::getenv("SWITCHDRIVE_PREVIEW_FONT");
        if (!fontPath) { error = "SWITCHDRIVE_PREVIEW_FONT"; shutdown(); return false; }
        data.fonts[index] = TTF_OpenFont(fontPath, pointSize);
        #endif
        if (!data.fonts[index]) { error = TTF_GetError(); shutdown(); return false; }
    }
    #ifdef __SWITCH__
    appendDiagnostic("ui: framebuffer create");
    Result framebufferResult = framebufferCreate(&data.framebuffer, nwindowGetDefault(), kWidth, kHeight, PIXEL_FORMAT_RGBA_8888, 1);
    if (R_FAILED(framebufferResult)) { error = "framebufferCreate"; shutdown(); return false; }
    data.framebufferReady = true;
    framebufferResult = framebufferMakeLinear(&data.framebuffer);
    if (R_FAILED(framebufferResult)) { error = "framebufferMakeLinear"; shutdown(); return false; }
    #endif
    data.screen = SDL_CreateRGBSurfaceWithFormat(0, kWidth, kHeight, 32, SDL_PIXELFORMAT_RGBA32);
    if (!data.screen) { error = SDL_GetError(); shutdown(); return false; }
    appendDiagnostic("ui: surface ready");
    #ifdef __SWITCH__
    if (data.romfsReady) data.logo = SDL_LoadBMP("romfs:/icon.bmp");
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeAny(&data.pad);
    hidInitializeTouchScreen();
    #else
    data.logo = SDL_LoadBMP("romfs/icon.bmp");
    #endif
    data.ready = true;
    appendDiagnostic("ui: ready");
    return true;
}

void Ui::diagnostic(const char* stage) const { appendDiagnostic(stage); }

void Ui::enableConsoleFallback(const std::string& error) {
    auto& data = *impl_;
    #ifdef __SWITCH__
    if (!data.console) data.console = consoleInit(nullptr);
    data.fallback = true;
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeAny(&data.pad);
    #endif
    (void)data; (void)error;
}

void Ui::shutdown() {
    if (!impl_) return;
    auto& data = *impl_;
    #ifdef __SWITCH__
    if (data.fallback && data.console) { consoleExit(data.console); data.console = nullptr; }
    #endif
    for (auto& [_, item] : data.textCache) if (item.surface) SDL_FreeSurface(item.surface);
    data.textCache.clear();
    for (TTF_Font*& font : data.fonts) { if (font) TTF_CloseFont(font); font = nullptr; }
    if (data.logo) { SDL_FreeSurface(data.logo); data.logo = nullptr; }
    if (data.screen) { SDL_FreeSurface(data.screen); data.screen = nullptr; }
    #ifdef __SWITCH__
    if (data.framebufferReady) { framebufferClose(&data.framebuffer); data.framebufferReady = false; }
    #endif
    if (data.ttfReady) { TTF_Quit(); data.ttfReady = false; }
    #ifdef __SWITCH__
    if (data.romfsReady) { romfsExit(); data.romfsReady = false; }
    if (data.plReady) { plExit(); data.plReady = false; }
    #endif
    data.ready = false;
}

#ifndef __SWITCH__
bool Ui::savePreview(const std::string& path, bool applet) {
    impl_->applet = applet;
    present();
    return impl_->screen && SDL_SaveBMP(impl_->screen, path.c_str()) == 0;
}
#endif

bool Ui::graphical() const { return impl_->ready; }
bool Ui::appletMode() const { return impl_->applet; }

void Ui::clear() {
    #ifdef __SWITCH__
    if (impl_->fallback) { consoleClear(); return; }
    #endif
    impl_->lines.clear();
    impl_->current.clear();
    impl_->hint.clear();
    impl_->cards.clear();
    impl_->rows.clear();
    impl_->subtitle.clear();
    impl_->progressCurrent = 0;
    impl_->progressTotal = 0;

}
void Ui::setBrand(const std::string& text) { impl_->brand = text; }
void Ui::setHeader(const std::string& title, const std::vector<std::string>& tabs, int activeTab) {
    auto& data = *impl_;
    if (data.activeTab != activeTab) data.focus.card = 0;
    if (tabs.empty() || data.tabs.empty()) data.focus.sidebar = false;
    data.header = title;
    data.tabs = tabs;
    data.activeTab = activeTab;
}
void Ui::setHint(const std::string& text) { impl_->hint = stripAnsi(text); }
void Ui::setCards(std::vector<Card> cards) {
    impl_->cards = std::move(cards);
    if (impl_->focus.card >= impl_->cards.size()) impl_->focus.card = 0;
}
void Ui::moveFocus(Direction direction) {
    auto& data = *impl_;
    const int section = data.focus.move(direction, data.cards.size(), data.activeTab, data.tabs.size());
    if (section >= 0) data.tabSelection = section;
}
uint64_t Ui::takeCardAction() { const auto result = impl_->cardAction; impl_->cardAction = 0; return result; }
void Ui::setRows(std::vector<Row> rows, size_t selected) { impl_->rows = std::move(rows); impl_->selectedRow = selected; }
void Ui::setSubtitle(const std::string& text) { impl_->subtitle = text; }
int Ui::takeRowSelection() { const int result = impl_->rowSelection; impl_->rowSelection = -1; return result; }
void Ui::setAppletWarning(const std::string& text) { impl_->appletWarning = stripAnsi(text); }
void Ui::setProgress(uint64_t current, uint64_t total) { impl_->progressCurrent = current; impl_->progressTotal = total; }

void Ui::write(const std::string& text) {
    auto& data = *impl_;
    if (data.fallback) { std::fputs(text.c_str(), stdout); return; }
    const std::string clean = stripAnsi(text);
    for (char character : clean) {
        if (character == '\r') data.current.clear();
        else if (character == '\n') { data.lines.push_back(data.current); data.current.clear(); }
        else data.current += character;
    }
}

void Ui::present() {
    auto& data = *impl_;
    #ifdef __SWITCH__
    if (data.fallback) { consoleUpdate(nullptr); return; }
    #endif
    if (!data.ready) return;
    ++data.frame;
    if (!data.screen) return;
    fillRect(data.screen, 0, 0, kWidth, kHeight, kBackground);
    // A permanent navigation rail leaves room for long translated page names.
    fillRect(data.screen, 0, 0, 244, kHeight, kSurface);
    if (data.logo) {
        SDL_Rect target{28, 32, 56, 56};
        SDL_BlitScaled(data.logo, nullptr, data.screen, &target);
    } else drawIcon(data.screen, Icon::Cloud, 28, 32, kAccent);
    data.drawText(data.brand, 28, 104, 1, kText, 204);
    for (size_t index = 0; index < data.tabs.size(); ++index) {
        const int y = 204 + static_cast<int>(index) * 76;
        const bool active = static_cast<int>(index) == data.activeTab;
        if (active) {
            if (data.focus.sidebar) roundedRect(data.screen, 14, y - 2, 216, 66, 16, kAccent);
            roundedRect(data.screen, 16, y, 212, 62, 14, kSelected);
        }
        if (active) roundedRect(data.screen, 16, y + 16, 4, 30, 2, kAccent);
        data.drawText(data.tabs[index], 40, y + 17, 0, active ? kAccent : kMuted, 180);
    }
    data.drawText(data.header, 292, 38, 2, kText, 924);
    data.drawText(data.subtitle, 294, 104, 0, kMuted, 924);
    const int contentY = data.applet ? 220 : 166;
    if (data.applet) {
        roundedRect(data.screen, 292, 150, 936, 54, 12, kWarningSurface);
        data.drawText(data.appletWarning, 310, 158, 0, kWarning, 896);
    }
    for (size_t index = 0; index < data.cards.size(); ++index) {
        const auto& card = data.cards[index];
        const int x = 292 + static_cast<int>(index % 2) * 478;
        const int y = contentY + static_cast<int>(index / 2) * 182;
        const bool focused = !data.focus.sidebar && index == data.focus.card;
        if (focused) roundedRect(data.screen, x - 3, y - 3, 464, 170, 23, kAccent);
        roundedRect(data.screen, x, y, 458, 164, 20, focused ? kSelected : kRaised);
        drawIcon(data.screen, card.icon, x + 24, y + 20, kAccent);
        SDL_Rect clip{x + 24, y + 76, 410, 76};
        SDL_SetClipRect(data.screen, &clip);
        data.drawText(card.title, x + 24, y + 76, 1, kText);
        data.drawText(card.detail, x + 24, y + 120, 0, kMuted);
        SDL_SetClipRect(data.screen, nullptr);
        if (focused || card.action != HidNpadButton_A) {
            const auto button = i18n::tr(focused ? i18n::TextId::ButtonA : card.action == HidNpadButton_X ? i18n::TextId::ButtonX : i18n::TextId::ButtonY);
            roundedRect(data.screen, x + 392, y + 22, 40, 40, 20, kSelected);
            data.drawText(button, x + 404, y + 28, 0, kAccent);
        }
    }
    if (!data.rows.empty()) {
        const size_t visible = data.applet ? 5 : 6;
        const size_t first = viewportStart(data.selectedRow, data.rows.size(), visible);
        for (size_t index = first; index < data.rows.size() && index < first + visible; ++index) {
            const auto box = rowBounds(index - first, data.applet);
            const auto& row = data.rows[index];
            const bool active = index == data.selectedRow;
            roundedRect(data.screen, box.x, box.y, box.width, box.height, 12, active ? kSelected : kSurface);
            if (active) roundedRect(data.screen, box.x, box.y + 12, 4, 38, 2, kAccent);
            drawIcon(data.screen, row.icon, box.x + 18, box.y + 9, active ? kAccent : kMuted);
            SDL_Rect clip{box.x + 78, box.y, 558, box.height};
            SDL_SetClipRect(data.screen, &clip);
            data.drawText(row.title, box.x + 78, box.y + 17, 0, kText, 0);
            SDL_SetClipRect(data.screen, nullptr);
            clip = {box.x + 664, box.y, 248, box.height};
            SDL_SetClipRect(data.screen, &clip);
            data.drawText(row.detail, box.x + 664, box.y + 17, 0, kMuted, 0);
            SDL_SetClipRect(data.screen, nullptr);
        }
        if (data.rows.size() > visible) {
            const int height = static_cast<int>(visible * 68);
            const int thumb = std::max(24, height * static_cast<int>(visible) / static_cast<int>(data.rows.size()));
            roundedRect(data.screen, 1240, contentY, 4, height, 2, kRaised);
            roundedRect(data.screen, 1240, contentY + (height - thumb) * static_cast<int>(first) / static_cast<int>(data.rows.size() - visible), 4, thumb, 2, kAccent);
        }
    }
    std::vector<std::string> lines = data.lines;
    if (!data.current.empty()) lines.push_back(data.current);
    if (!lines.empty()) {
        // Status and confirmation screens are wrapped prose, never terminal rows.
        roundedRect(data.screen, 292, contentY, 936, 376 - (data.applet ? 54 : 0), 20, kSurface);
        SDL_Rect clip{316, contentY + 18, 884, 318 - (data.applet ? 54 : 0)};
        SDL_SetClipRect(data.screen, &clip);
        int y = contentY + 24;
        for (const auto& line : lines) {
            if (line.empty()) { y += 12; continue; }
            const auto item = data.text(line.substr(0, 1024), 0, kText, 864);
            if (!item.surface) continue;
            SDL_Rect target{320, y, item.width, item.height};
            SDL_BlitSurface(item.surface, nullptr, data.screen, &target);
            y += item.height + 12;
            if (y > clip.y + clip.h) break;
        }
        SDL_SetClipRect(data.screen, nullptr);
    }
    if (data.progressTotal) {
        const double ratio = std::min(1.0, static_cast<double>(data.progressCurrent) / static_cast<double>(data.progressTotal));
        roundedRect(data.screen, 316, 568, 888, 12, 6, kRaised);
        roundedRect(data.screen, 316, 568, static_cast<int>(888 * ratio), 12, 6, kAccent);
    }
    fillRect(data.screen, 276, 622, 952, 1, kRaised);
    data.drawText(data.hint, 292, 641, 0, kMuted, 924);
    #ifdef __SWITCH__
    u32 stride{};
    auto* output = static_cast<unsigned char*>(framebufferBegin(&data.framebuffer, &stride));
    if (output) {
        const auto* input = static_cast<const unsigned char*>(data.screen->pixels);
        for (int row = 0; row < kHeight; ++row) std::memcpy(output + static_cast<size_t>(row) * stride, input + static_cast<size_t>(row) * data.screen->pitch, kWidth * 4);
    }
    framebufferEnd(&data.framebuffer);
    #endif
}

void Ui::scanInput() {
    #ifdef __SWITCH__
    auto& data = *impl_;
    data.pressed = 0;
    data.tabSelection = -1;
    data.rowSelection = -1;
    data.cardAction = 0;
    padUpdate(&data.pad);
    data.pressed = padGetButtonsDown(&data.pad);
    // Read both sticks as well as the D-pad; normalize held directions before
    // detecting repeats so either Joy-Con can navigate without frame-rate drift.
    const uint64_t held = padGetButtons(&data.pad);
    constexpr uint64_t directionMask = HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right;
    uint64_t directions = held & directionMask;
    for (unsigned index = 0; index < 2; ++index) {
        const auto stick = padGetStickPos(&data.pad, index);
        if (stick.y > 16000) directions |= HidNpadButton_Up;
        if (stick.y < -16000) directions |= HidNpadButton_Down;
        if (stick.x < -16000) directions |= HidNpadButton_Left;
        if (stick.x > 16000) directions |= HidNpadButton_Right;
    }
    const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    data.pressed = (data.pressed & ~directionMask) | data.directionRepeat.update(directions, now);

    if (data.fallback) return;
    const bool menu = !data.cards.empty() && !data.tabs.empty() && data.lines.empty() && data.current.empty();
    if (menu) {
        // Section changes consume this input update, avoiding activation in a
        // different section from the one the user saw when pressing A.
        if (data.pressed & (HidNpadButton_L | HidNpadButton_R)) {
            data.tabSelection = static_cast<int>(moveSelection(static_cast<size_t>(data.activeTab), data.tabs.size(), data.pressed & HidNpadButton_R ? 1 : -1));
            data.focus = {};
        } else {
            if (data.pressed & HidNpadButton_Up) moveFocus(Direction::Up);
            else if (data.pressed & HidNpadButton_Down) moveFocus(Direction::Down);
            else if (data.pressed & HidNpadButton_Left) moveFocus(Direction::Left);
            else if (data.pressed & HidNpadButton_Right) moveFocus(Direction::Right);
            if (data.pressed & HidNpadButton_B) data.focus.sidebar = true;
            if (data.tabSelection < 0) {
                if (data.pressed & HidNpadButton_A) {
                    const int selected = data.focus.activate(data.cards.size());
                    if (selected >= 0) data.cardAction = data.cards[static_cast<size_t>(selected)].action;
                } else if (data.pressed & (HidNpadButton_X | HidNpadButton_Y)) {
                    for (size_t index = 0; index < data.cards.size(); ++index) {
                        if (data.cards[index].action & data.pressed) {
                            data.focus = {index, false};
                            data.cardAction = data.cards[index].action;
                            break;
                        }
                    }
                }
            }
        }
    }
    HidTouchScreenState touch{};
    hidGetTouchScreenStates(&touch, 1);
    const bool down = touch.count > 0;
    if (down && !data.touchDown) {
        data.touchStartX = static_cast<int>(touch.touches[0].x);
        data.touchStartY = data.lastTouchY = static_cast<int>(touch.touches[0].y);
        data.dragged = false;
    } else if (!down && data.touchDown && !data.dragged) {
        // Activate only on release: starting a swipe on the focused folder
        // must not open it before the user's finger has moved.
        const int x = data.touchStartX;
        const int y = data.touchStartY;
        if (!data.tabs.empty() && x >= 16 && x < 228 && y >= 204 && y < 508) {
            const int index = (y - 204) / 76;
            if (index < static_cast<int>(data.tabs.size()) && (y - 204) % 76 < 62) { data.tabSelection = index; data.focus = {0, true}; data.cardAction = 0; }
        }
        for (size_t index = 0; data.lines.empty() && index < data.cards.size(); ++index) {
            const HitBox box{292 + static_cast<int>(index % 2) * 478, (data.applet ? 220 : 166) + static_cast<int>(index / 2) * 182, 458, 164};
            if (hitTest(box, x, y)) {
                data.focus = {index, false};
                data.cardAction = data.cards[index].action;
            }
        }
        if (data.lines.empty()) data.rowSelection = touchedRow(x, y, data.selectedRow, data.rows.size(), data.applet);
        if (data.rowSelection >= 0 && static_cast<size_t>(data.rowSelection) == data.selectedRow) data.pressed |= HidNpadButton_A;
    } else if (down && data.touchDown) {
        const int y = static_cast<int>(touch.touches[0].y);
        if (std::abs(y - data.touchStartY) >= 16 || std::abs(static_cast<int>(touch.touches[0].x) - data.touchStartX) >= 16) data.dragged = true;
        // Treat a finger drag as a list scroll.  The threshold prevents a
        // normal tap from changing focus before it confirms the selected row.
        if (data.lines.empty() && !data.rows.empty() && y >= (data.applet ? 220 : 166) && y < 620 && std::abs(y - data.lastTouchY) >= 36) {
            data.dragged = true;
            data.pressed |= y < data.lastTouchY ? HidNpadButton_Down : HidNpadButton_Up;
            data.lastTouchY = y;
        }
    }
    data.touchDown = down;
    #endif
}

uint64_t Ui::keysDown() const { return impl_->pressed; }
int Ui::takeTabSelection() { const int value = impl_->tabSelection; impl_->tabSelection = -1; return value; }

Ui& instance() { static Ui ui; return ui; }

int writef(const char* format, ...) {
    std::array<char, 4096> buffer{};
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);
    instance().write(buffer.data());
    return written;
}

void clear() { instance().clear(); }
void present() { instance().present(); }
void scanInput() { instance().scanInput(); }
uint64_t keysDown() { return instance().keysDown(); }

} // namespace switchdrive::ui
