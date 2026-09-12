#include "switchdrive/ui_model.hpp"

namespace switchdrive::ui {

int MenuFocus::move(Direction direction, size_t count, int section, size_t sections) {
    if (!count) { card = 0; sidebar = true; }
    else if (card >= count) card = count - 1;
    if (sidebar) {
        if (direction == Direction::Right && count) sidebar = false;
        if (section >= 0 && sections && (direction == Direction::Up || direction == Direction::Down))
            return static_cast<int>(moveSelection(static_cast<size_t>(section), sections, direction == Direction::Up ? -1 : 1));
        return -1;
    }
    switch (direction) {
        case Direction::Left: if (card % 2) --card; else sidebar = true; break;
        case Direction::Right: if (card % 2 == 0 && card + 1 < count) ++card; break;
        case Direction::Up: if (card >= 2) card -= 2; break;
        case Direction::Down:
            if ((card / 2 + 1) * 2 < count) card = card + 2 < count ? card + 2 : count - 1;
            break;
    }
    return -1;
}

int MenuFocus::activate(size_t count) {
    if (!count) return -1;
    if (sidebar) { sidebar = false; return -1; }
    if (card >= count) card = count - 1;
    return static_cast<int>(card);
}

uint64_t DirectionRepeat::update(uint64_t directions, uint64_t milliseconds) {
    if (directions != held) {
        held = directions;
        next = milliseconds + 350;
        return held;
    }
    if (!held || milliseconds < next) return 0;
    next = milliseconds + 100;
    return held;
}

HitBox rowBounds(size_t visibleIndex, bool applet) {
    return {292, (applet ? 220 : 166) + static_cast<int>(visibleIndex) * 68, 936, 62};
}

int touchedRow(int x, int y, size_t selected, size_t count, bool applet) {
    const size_t visible = applet ? 5 : 6;
    const size_t first = viewportStart(selected, count, visible);
    for (size_t index = first; index < count && index < first + visible; ++index)
        if (hitTest(rowBounds(index - first, applet), x, y)) return static_cast<int>(index);
    return -1;
}

bool hitTest(HitBox box, int x, int y) {
    return x >= box.x && y >= box.y && x < box.x + box.width && y < box.y + box.height;
}

size_t moveSelection(size_t selected, size_t count, int delta, bool wrap) {
    if (!count) return 0;
    if (delta < 0) {
        const size_t amount = static_cast<size_t>(-delta);
        return wrap ? (selected + count - amount % count) % count : (amount > selected ? 0 : selected - amount);
    }
    const size_t amount = static_cast<size_t>(delta);
    return wrap ? (selected + amount) % count : (selected + amount >= count ? count - 1 : selected + amount);
}

size_t viewportStart(size_t selected, size_t count, size_t visible) {
    if (!visible || count <= visible) return 0;
    const size_t maximum = count - visible;
    return selected >= visible ? (selected - visible + 1 > maximum ? maximum : selected - visible + 1) : 0;
}

} // namespace switchdrive::ui
