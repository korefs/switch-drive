#pragma once

#include <memory>
#include <string>

namespace switchdrive {
class AppController;
}

namespace switchdrive::ui {

class Ui {
  public:
    Ui();
    ~Ui();
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    bool initialize(const std::string& locale, std::string& error);
    int run(AppController& controller);
    void shutdown();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace switchdrive::ui
