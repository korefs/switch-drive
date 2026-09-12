// Test-only service boundary for compiling the actual __SWITCH__ UI path on
// the host. This models buffer ownership, not the Switch OS or HID driver.
#pragma once
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

using u32 = uint32_t;
using Result = u32;
constexpr bool R_FAILED(Result r) { return r != 0; }
constexpr bool R_SUCCEEDED(Result r) { return r == 0; }
constexpr int AppletType_Application = 0, AppletType_LibraryApplet = 1;
constexpr int AppletFocusState_InFocus = 0;
constexpr int PlServiceType_User = 0, PlSharedFontType_Standard = 0;
constexpr int PIXEL_FORMAT_RGBA_8888 = 1;
constexpr u32 HidNpadStyleSet_NpadStandard = 0x1f;
constexpr uint64_t HidNpadButton_A = 1ULL << 0, HidNpadButton_B = 1ULL << 1;
constexpr uint64_t HidNpadButton_X = 1ULL << 2, HidNpadButton_Y = 1ULL << 3;
constexpr uint64_t HidNpadButton_L = 1ULL << 6, HidNpadButton_R = 1ULL << 7;
constexpr uint64_t HidNpadButton_Plus = 1ULL << 10;
constexpr uint64_t HidNpadButton_Left = 1ULL << 12, HidNpadButton_Up = 1ULL << 13;
constexpr uint64_t HidNpadButton_Right = 1ULL << 14, HidNpadButton_Down = 1ULL << 15;
constexpr uint64_t HidNpadButton_AnyLeft = HidNpadButton_Left | (1ULL << 16) | (1ULL << 20);
constexpr uint64_t HidNpadButton_AnyUp = HidNpadButton_Up | (1ULL << 17) | (1ULL << 21);
constexpr uint64_t HidNpadButton_AnyRight = HidNpadButton_Right | (1ULL << 18) | (1ULL << 22);
constexpr uint64_t HidNpadButton_AnyDown = HidNpadButton_Down | (1ULL << 19) | (1ULL << 23);

struct HidAnalogStickState { int x{}, y{}; };
struct PadState {
    uint64_t current{}, previous{};
    bool connected{};
    std::array<HidAnalogStickState, 2> sticks{};
};
struct HidTouchScreenState {
    int count{};
    struct Touch { u32 x{}, y{}; } touches[1];
};
namespace simulated {
inline int appletType = AppletType_Application, focus = AppletFocusState_InFocus;
inline bool connected{};
inline uint64_t buttons{}, polls{}, frames{};
inline std::array<HidAnalogStickState, 2> sticks{};
inline std::vector<char> font;
}
inline int appletGetAppletType() { return simulated::appletType; }
inline int appletGetFocusState() { return simulated::focus; }
inline bool envHasHeapOverride() { return false; }
inline uint64_t envGetHeapOverrideSize() { return 0; }
inline Result plInitialize(int) { return 0; }
inline void plExit() {}
struct PlFontData { void* address{}; size_t size{}; };
inline Result plGetSharedFontByType(PlFontData* output, int) {
    const char* path = std::getenv("SWITCHDRIVE_PREVIEW_FONT");
    if (!path) throw std::runtime_error("Set SWITCHDRIVE_PREVIEW_FONT to a host TTF font");
    std::ifstream input(path, std::ios::binary);
    simulated::font.assign(std::istreambuf_iterator<char>(input), {});
    *output = {simulated::font.data(), simulated::font.size()};
    return simulated::font.empty() ? 1 : 0;
}
inline Result romfsInit() { return 1; }
inline void romfsExit() {}
inline void padConfigureInput(u32, u32) {}
inline void padInitializeAny(PadState* pad) { *pad = {}; }
inline void padUpdate(PadState* pad) {
    ++simulated::polls;
    pad->previous = pad->current;
    pad->current = simulated::connected ? simulated::buttons : 0;
    pad->connected = simulated::connected;
    pad->sticks = simulated::connected ? simulated::sticks : std::array<HidAnalogStickState, 2>{};
}
inline bool padIsConnected(const PadState* pad) { return pad->connected; }
inline uint64_t padGetButtonsDown(const PadState* pad) { return pad->current & ~pad->previous; }
inline uint64_t padGetButtons(const PadState* pad) { return pad->current; }
inline HidAnalogStickState padGetStickPos(const PadState* pad, unsigned index) { return pad->sticks.at(index); }
inline void hidInitializeTouchScreen() {}
inline void hidGetTouchScreenStates(HidTouchScreenState* state, int) { *state = {}; }

struct Framebuffer {
    std::vector<std::vector<unsigned char>> slots;
    int displayed{-1}, dequeued{-1};
    u32 stride{};
};
inline void* nwindowGetDefault() { return nullptr; }
inline Result framebufferCreate(Framebuffer* fb, void*, u32 width, u32 height, int, u32 count) {
    fb->stride = width * 4;
    fb->slots.resize(count, std::vector<unsigned char>(fb->stride * height));
    return 0;
}
inline Result framebufferMakeLinear(Framebuffer*) { return 0; }
inline void* framebufferBegin(Framebuffer* fb, u32* stride) {
    if (fb->dequeued >= 0) throw std::runtime_error("Frame already dequeued");
    // The consumer retains the displayed buffer until a replacement is queued.
    // Real nwindowDequeueBuffer waits here indefinitely if no slot is free.
    for (size_t slot = 0; slot < fb->slots.size(); ++slot) {
        if (static_cast<int>(slot) == fb->displayed) continue;
        fb->dequeued = static_cast<int>(slot);
        *stride = fb->stride;
        return fb->slots[slot].data();
    }
    throw std::runtime_error("No free framebuffer: displayed buffer retained by consumer");
}
inline void framebufferEnd(Framebuffer* fb) {
    if (fb->dequeued < 0) throw std::runtime_error("No frame dequeued");
    fb->displayed = fb->dequeued;
    fb->dequeued = -1;
    ++simulated::frames;
}
inline void framebufferClose(Framebuffer* fb) { *fb = {}; }
struct PrintConsole {};
inline PrintConsole* consoleInit(void*) { static PrintConsole console; return &console; }
inline void consoleExit(PrintConsole*) {}
inline void consoleClear() {}
inline void consoleUpdate(void*) {}
