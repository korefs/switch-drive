// SPDX-License-Identifier: GPL-3.0-or-later
#include "switchdrive/qr.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace switchdrive::qr {
namespace {

struct Block { int total, data; };
using Modules = std::vector<int8_t>;

constexpr std::array<std::array<int, 3>, 10> kMediumBlocks{{
    {{1, 26, 16}}, {{1, 44, 28}}, {{1, 70, 44}}, {{2, 50, 32}}, {{2, 67, 43}},
    {{4, 43, 27}}, {{4, 49, 31}}, {{2, 60, 38}}, {{3, 58, 36}}, {{4, 69, 43}}
}};
constexpr std::array<std::array<int, 3>, 3> kMediumExtraBlocks{{
    {{2, 61, 39}}, {{2, 59, 37}}, {{1, 70, 44}}
}};
constexpr std::array<std::array<int, 3>, 10> kAlignment{{
    {{0, 0, 0}}, {{6, 18, 0}}, {{6, 22, 0}}, {{6, 26, 0}}, {{6, 30, 0}},
    {{6, 34, 0}}, {{6, 22, 38}}, {{6, 24, 42}}, {{6, 26, 46}}, {{6, 28, 50}}
}};

int bitLength(int value) {
    int result{};
    while (value) { ++result; value >>= 1; }
    return result;
}

int bchFormat(int value) {
    int remainder = value << 10;
    constexpr int polynomial = 0x537;
    while (bitLength(remainder) >= bitLength(polynomial)) remainder ^= polynomial << (bitLength(remainder) - bitLength(polynomial));
    return ((value << 10) | remainder) ^ 0x5412;
}

int bchVersion(int value) {
    int remainder = value << 12;
    constexpr int polynomial = 0x1f25;
    while (bitLength(remainder) >= bitLength(polynomial)) remainder ^= polynomial << (bitLength(remainder) - bitLength(polynomial));
    return (value << 12) | remainder;
}

uint8_t multiply(uint8_t left, uint8_t right) {
    uint8_t result{};
    while (right) {
        if (right & 1) result ^= left;
        const bool high = left & 0x80;
        left <<= 1;
        if (high) left ^= 0x1d;
        right >>= 1;
    }
    return result;
}

uint8_t power(uint8_t value, int exponent) {
    uint8_t result{1};
    while (exponent--) result = multiply(result, value);
    return result;
}

std::vector<uint8_t> generator(int degree) {
    std::vector<uint8_t> polynomial{1};
    for (int exponent = 0; exponent < degree; ++exponent) {
        std::vector<uint8_t> next(polynomial.size() + 1);
        const uint8_t root = power(2, exponent);
        for (size_t index = 0; index < polynomial.size(); ++index) {
            next[index] ^= polynomial[index];
            next[index + 1] ^= multiply(polynomial[index], root);
        }
        polynomial = std::move(next);
    }
    return polynomial;
}

std::vector<uint8_t> correction(const std::vector<uint8_t>& data, int degree) {
    const auto divisor = generator(degree);
    std::vector<uint8_t> remainder(data.size() + static_cast<size_t>(degree));
    std::copy(data.begin(), data.end(), remainder.begin());
    for (size_t index = 0; index < data.size(); ++index) {
        const uint8_t coefficient = remainder[index];
        if (!coefficient) continue;
        for (size_t term = 1; term < divisor.size(); ++term) remainder[index + term] ^= multiply(divisor[term], coefficient);
    }
    return {remainder.end() - degree, remainder.end()};
}

void append(std::vector<bool>& bits, uint32_t value, int count) {
    for (int bit = count - 1; bit >= 0; --bit) bits.push_back((value >> bit) & 1U);
}

std::vector<Block> blocksFor(int version) {
    const auto& first = kMediumBlocks[static_cast<size_t>(version - 1)];
    std::vector<Block> blocks(static_cast<size_t>(first[0]), {first[1], first[2]});
    if (version >= 8) {
        const auto& extra = kMediumExtraBlocks[static_cast<size_t>(version - 8)];
        blocks.insert(blocks.end(), static_cast<size_t>(extra[0]), {extra[1], extra[2]});
    }
    return blocks;
}

int dataCapacity(int version) {
    int result{};
    for (const auto& block : blocksFor(version)) result += block.data;
    return result;
}

std::vector<uint8_t> makeData(std::string_view content, int version) {
    std::vector<bool> bits;
    append(bits, 0x4, 4);
    append(bits, static_cast<uint32_t>(content.size()), version < 10 ? 8 : 16);
    for (const unsigned char byte : content) append(bits, byte, 8);
    const int capacity = dataCapacity(version) * 8;
    append(bits, 0, std::min(4, capacity - static_cast<int>(bits.size())));
    while (bits.size() % 8) bits.push_back(false);
    std::vector<uint8_t> data;
    for (size_t index = 0; index < bits.size(); index += 8) {
        uint8_t byte{};
        for (size_t bit = 0; bit < 8; ++bit) if (bits[index + bit]) byte |= 0x80 >> bit;
        data.push_back(byte);
    }
    for (bool alternate{}; static_cast<int>(data.size()) < capacity / 8; alternate = !alternate) data.push_back(alternate ? 0x11 : 0xec);

    const auto blocks = blocksFor(version);
    std::vector<std::vector<uint8_t>> dataBlocks, correctionBlocks;
    size_t offset{};
    for (const auto& block : blocks) {
        dataBlocks.emplace_back(data.begin() + static_cast<ptrdiff_t>(offset), data.begin() + static_cast<ptrdiff_t>(offset + block.data));
        correctionBlocks.push_back(correction(dataBlocks.back(), block.total - block.data));
        offset += block.data;
    }
    std::vector<uint8_t> result;
    const size_t largestData = std::max_element(dataBlocks.begin(), dataBlocks.end(), [](const auto& left, const auto& right) { return left.size() < right.size(); })->size();
    const size_t largestCorrection = correctionBlocks.front().size();
    for (size_t index = 0; index < largestData; ++index) for (const auto& block : dataBlocks) if (index < block.size()) result.push_back(block[index]);
    for (size_t index = 0; index < largestCorrection; ++index) for (const auto& block : correctionBlocks) if (index < block.size()) result.push_back(block[index]);
    return result;
}

bool masked(int mask, int row, int column) {
    switch (mask) {
        case 0: return (row + column) % 2 == 0;
        case 1: return row % 2 == 0;
        case 2: return column % 3 == 0;
        case 3: return (row + column) % 3 == 0;
        case 4: return (row / 2 + column / 3) % 2 == 0;
        case 5: return row * column % 2 + row * column % 3 == 0;
        case 6: return (row * column % 2 + row * column % 3) % 2 == 0;
        default: return (row * column % 3 + (row + column) % 2) % 2 == 0;
    }
}

void set(Modules& modules, int size, int row, int column, bool dark) { modules[static_cast<size_t>(row * size + column)] = dark; }

void finder(Modules& modules, int size, int top, int left) {
    for (int row = -1; row <= 7; ++row) for (int column = -1; column <= 7; ++column) {
        if (top + row < 0 || top + row >= size || left + column < 0 || left + column >= size) continue;
        set(modules, size, top + row, left + column, row >= 0 && row <= 6 && column >= 0 && column <= 6 &&
            (row == 0 || row == 6 || column == 0 || column == 6 || (row >= 2 && row <= 4 && column >= 2 && column <= 4)));
    }
}

void format(Modules& modules, int size, int mask, bool test) {
    const int bits = bchFormat(mask); // Medium is encoded as error-correction value 0.
    for (int index = 0; index < 15; ++index) {
        const bool dark = !test && ((bits >> index) & 1);
        set(modules, size, index < 6 ? index : index < 8 ? index + 1 : size - 15 + index, 8, dark);
        set(modules, size, 8, index < 8 ? size - index - 1 : index < 9 ? 15 - index : 14 - index, dark);
    }
    set(modules, size, size - 8, 8, !test);
}

Modules build(int version, const std::vector<uint8_t>& data, int mask, bool test) {
    const int size = version * 4 + 17;
    Modules modules(static_cast<size_t>(size * size), -1);
    finder(modules, size, 0, 0); finder(modules, size, size - 7, 0); finder(modules, size, 0, size - 7);
    const auto& positions = kAlignment[static_cast<size_t>(version - 1)];
    for (int rowCenter : positions) for (int columnCenter : positions) {
        if (!rowCenter || !columnCenter || modules[static_cast<size_t>(rowCenter * size + columnCenter)] != -1) continue;
        for (int row = -2; row <= 2; ++row) for (int column = -2; column <= 2; ++column) set(modules, size, rowCenter + row, columnCenter + column, std::abs(row) == 2 || std::abs(column) == 2 || (!row && !column));
    }
    for (int index = 8; index < size - 8; ++index) {
        if (modules[static_cast<size_t>(index * size + 6)] == -1) set(modules, size, index, 6, index % 2 == 0);
        if (modules[static_cast<size_t>(6 * size + index)] == -1) set(modules, size, 6, index, index % 2 == 0);
    }
    format(modules, size, mask, test);
    if (version >= 7) {
        const int bits = bchVersion(version);
        for (int index = 0; index < 18; ++index) {
            const bool dark = !test && ((bits >> index) & 1);
            set(modules, size, index / 3, index % 3 + size - 11, dark);
            set(modules, size, index % 3 + size - 11, index / 3, dark);
        }
    }
    int row = size - 1, direction = -1, bit = 7;
    size_t byte{};
    for (int column = size - 1; column > 0; column -= 2) {
        if (column == 6) --column;
        while (true) {
            for (int offset = 0; offset < 2; ++offset) if (modules[static_cast<size_t>(row * size + column - offset)] == -1) {
                bool dark = byte < data.size() && ((data[byte] >> bit) & 1);
                if (masked(mask, row, column - offset)) dark = !dark;
                set(modules, size, row, column - offset, dark);
                if (!bit--) { bit = 7; ++byte; }
            }
            row += direction;
            if (row < 0 || row == size) { row -= direction; direction = -direction; break; }
        }
    }
    return modules;
}

int penalty(const Modules& modules, int size) {
    int result{};
    const auto dark = [&](int row, int column) { return modules[static_cast<size_t>(row * size + column)] != 0; };
    for (int row = 0; row < size; ++row) for (int column = 0; column < size; ++column) {
        int neighbours{};
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) if ((dy || dx) && row + dy >= 0 && row + dy < size && column + dx >= 0 && column + dx < size && dark(row, column) == dark(row + dy, column + dx)) ++neighbours;
        if (neighbours > 5) result += neighbours - 2;
    }
    for (int row = 0; row + 1 < size; ++row) for (int column = 0; column + 1 < size; ++column) {
        const int count = dark(row, column) + dark(row + 1, column) + dark(row, column + 1) + dark(row + 1, column + 1);
        if (count == 0 || count == 4) result += 3;
    }
    for (int row = 0; row < size; ++row) for (int column = 0; column + 6 < size; ++column) if (dark(row, column) && !dark(row, column + 1) && dark(row, column + 2) && dark(row, column + 3) && dark(row, column + 4) && !dark(row, column + 5) && dark(row, column + 6)) result += 40;
    for (int column = 0; column < size; ++column) for (int row = 0; row + 6 < size; ++row) if (dark(row, column) && !dark(row + 1, column) && dark(row + 2, column) && dark(row + 3, column) && dark(row + 4, column) && !dark(row + 5, column) && dark(row + 6, column)) result += 40;
    int darkCount{};
    for (const auto value : modules) darkCount += value != 0;
    return result + std::abs(100 * darkCount / (size * size) - 50) / 5 * 10;
}

} // namespace

std::optional<Code> encode(std::string_view content) {
    if (content.size() > 213) return std::nullopt;
    int version = 1;
    for (; version <= 10; ++version) if (4 + (version < 10 ? 8 : 16) + static_cast<int>(content.size()) * 8 <= dataCapacity(version) * 8) break;
    if (version > 10) return std::nullopt;
    const auto data = makeData(content, version);
    int bestMask{};
    int bestPenalty{};
    for (int mask = 0; mask < 8; ++mask) {
        const int value = penalty(build(version, data, mask, true), version * 4 + 17);
        if (!mask || value < bestPenalty) { bestMask = mask; bestPenalty = value; }
    }
    const int size = version * 4 + 17;
    const auto modules = build(version, data, bestMask, false);
    Code code{size, {}};
    code.modules.reserve(modules.size());
    for (const auto module : modules) code.modules.push_back(module != 0);
    return code;
}

} // namespace switchdrive::qr
