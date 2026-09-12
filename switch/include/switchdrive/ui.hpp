#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "switchdrive/ui_model.hpp"

namespace switchdrive::ui {

enum class Icon { Cloud, Folder, File, Library, Settings, Language };

struct Row {
    std::string title;
    std::string detail;
    Icon icon{Icon::File};
};

struct Card {
    std::string title;
    std::string detail;
    uint64_t action{};
    Icon icon{Icon::Cloud};
};

class Ui {
  public:
    Ui();
    ~Ui();
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    bool initialize(std::string& error);
    void diagnostic(const char* stage) const;
    void enableConsoleFallback(const std::string& error);
    void shutdown();
    bool graphical() const;
#ifndef __SWITCH__
    bool savePreview(const std::string& path, bool applet = false);
#endif

    void clear();
    void setBrand(const std::string& text);
    void setHeader(const std::string& title, const std::vector<std::string>& tabs = {}, int activeTab = -1);
    void setHint(const std::string& text);
    void setCards(std::vector<Card> cards);
    void moveFocus(Direction direction);
    uint64_t takeCardAction();
    void setRows(std::vector<Row> rows, size_t selected);
    void setSubtitle(const std::string& text);
    int takeRowSelection();
    void setAppletWarning(const std::string& text);
    void setProgress(uint64_t current, uint64_t total);
    void write(const std::string& text);
    void present();
    void scanInput();
    uint64_t keysDown() const;
    int takeTabSelection();
    bool appletMode() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Ui& instance();
int writef(const char* format, ...);
void clear();
void present();
void scanInput();
uint64_t keysDown();

} // namespace switchdrive::ui
