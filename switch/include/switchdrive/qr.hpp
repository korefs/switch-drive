#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace switchdrive::qr {

// A byte-mode QR Code with medium error correction. Pairing links are ASCII,
// but byte mode also keeps percent-encoded URLs intact.
struct Code {
    int size{};
    std::vector<bool> modules;

    bool dark(int row, int column) const { return modules[static_cast<size_t>(row * size + column)]; }
};

// Supports the first ten QR versions (up to 213 URL bytes at level M).
std::optional<Code> encode(std::string_view content);

} // namespace switchdrive::qr
