#pragma once

#include <cstddef>
#include <cstdint>

namespace switchdrive::ui {

struct HitBox { int x, y, width, height; };

enum class Direction { Up, Down, Left, Right };

struct MenuFocus {
    size_t card{};
    bool sidebar{};
    // Returns a requested section, or -1 when only card focus changed.
    int move(Direction direction, size_t count, int section, size_t sections);
    // A enters the cards from the sidebar, then activates the focused card.
    int activate(size_t count);
};

struct DirectionRepeat {
    uint64_t held{};
    uint64_t next{};
    uint64_t update(uint64_t directions, uint64_t milliseconds);
};

HitBox rowBounds(size_t visibleIndex, bool applet);
int touchedRow(int x, int y, size_t selected, size_t count, bool applet);
bool hitTest(HitBox box, int x, int y);
size_t moveSelection(size_t selected, size_t count, int delta, bool wrap = true);
size_t viewportStart(size_t selected, size_t count, size_t visible);

} // namespace switchdrive::ui
