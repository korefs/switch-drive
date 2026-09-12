#include "switchdrive/ui.hpp"
#include <switch.h>
#include <iostream>

using switchdrive::ui::Ui;
using switchdrive::ui::Icon;

void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}

void run(int mode, bool initiallyConnected) {
    simulated::appletType = mode;
    simulated::connected = initiallyConnected;
    simulated::buttons = simulated::polls = simulated::frames = 0;
    simulated::sticks = {};
    simulated::focus = AppletFocusState_InFocus;
    Ui view;
    std::string error;
    if (!view.initialize(error)) throw std::runtime_error(error);
    require(view.controllerConnected() == initiallyConnected, "Initial status was not sampled");
    view.setBrand("Switch Drive");
    view.setHeader("Home", {"Home", "Files", "Library", "Settings"}, 0);
    view.setCards({{"Connect", "", HidNpadButton_A, Icon::Cloud},
                   {"Files", "", HidNpadButton_X, Icon::Folder},
                   {"Library", "", HidNpadButton_Y, Icon::Library}});
    const auto tick = [&](uint64_t buttons) {
        simulated::buttons = buttons;
        view.scanInput();
        view.present();
    };
    // Reproduce a first screen with no controller samples, followed by a
    // connection. A one-buffer renderer fails here before users can navigate.
    tick(0);
    simulated::connected = true;
    tick(0);
    require(view.controllerConnected(), "Delayed connection was not observed");
    tick(HidNpadButton_Right);
    tick(HidNpadButton_A);
    require(view.takeCardAction() == HidNpadButton_X, "A did not activate focused Files card");
    tick(0);
    simulated::sticks[1].y = -32767;
    tick(0);
    simulated::sticks = {};
    tick(HidNpadButton_A);
    require(view.takeCardAction() == HidNpadButton_Y, "Right stick did not select Library");
    tick(0);
    simulated::connected = false;
    tick(HidNpadButton_A);
    require(!view.controllerConnected() && !view.keysDown(), "Disconnected input remained active");
    simulated::connected = true;
    tick(0);
    tick(HidNpadButton_L);
    require(view.takeTabSelection() == 3, "L did not change section");
    simulated::focus = 1;
    tick(0);
    require(!view.inputFocused(), "Focus loss not reported");
    simulated::focus = AppletFocusState_InFocus;
    for (int frame = 0; frame < 300; ++frame) tick(0);
    const auto beforeExit = simulated::frames;
    simulated::buttons = HidNpadButton_Plus;
    view.scanInput();
    require(view.keysDown() & HidNpadButton_Plus, "Plus was not readable after repeated presentation");
    require(simulated::frames == beforeExit, "Input scan unexpectedly presented a frame");
    require(simulated::frames >= 300 && simulated::polls > simulated::frames, "Loop did not keep polling");
    view.shutdown();
    std::cout << "mode=" << mode << " initially_connected=" << initiallyConnected
              << " frames=" << simulated::frames << " input_polls=" << simulated::polls << " passed\n";
}

int main() {
    try {
        for (const int mode : {AppletType_Application, AppletType_LibraryApplet})
            for (const bool connected : {false, true}) run(mode, connected);
    } catch (const std::exception& error) {
        std::cerr << "UI runtime regression: " << error.what() << '\n';
        return 1;
    }
}
