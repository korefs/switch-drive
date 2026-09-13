// SPDX-License-Identifier: GPL-3.0-or-later
#include "switchdrive/core.hpp"
#include "switchdrive/i18n.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

#include <mbedtls/aes.h>
#include <zstd.h>

namespace switchdrive {
namespace {

constexpr uint64_t kNcaHeaderSize = 0x4000;
constexpr size_t kSectionSize = 0x40;
constexpr size_t kIoSize = 256 * 1024;

struct NczSection {
    uint64_t offset{}, size{}, cryptoType{};
    std::array<uint8_t, 16> key{}, counter{};
};

struct NczBlock {
    uint64_t dataOffset{}, decompressedSize{}, blockSize{};
    std::vector<uint32_t> compressedSizes;
};

struct NczLayout {
    uint64_t decompressedSize{}, dataOffset{};
    std::vector<NczSection> sections;
    bool blockCompressed{};
    NczBlock block;
};

uint32_t readLe32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t readLe64(const uint8_t* bytes) {
    uint64_t value{};
    for (unsigned i = 0; i < 8; ++i) value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    return value;
}

bool readNcz(const Pfs0& pfs0, const Pfs0Entry& entry, uint64_t offset, void* data, size_t size, std::string& error) {
    return pfs0.read(entry, offset, data, size, error);
}

bool parseNcz(const Pfs0& pfs0, const Pfs0Entry& entry, NczLayout& layout, std::string& error) {
    layout = {};
    if (entry.size < kNcaHeaderSize + 16) { error = i18n::tr(i18n::TextId::NczInvalidHeader); return false; }
    std::array<uint8_t, 16> prefix{};
    if (!readNcz(pfs0, entry, kNcaHeaderSize, prefix.data(), prefix.size(), error)) return false;
    if (std::memcmp(prefix.data(), "NCZSECTN", 8) != 0) { error = i18n::tr(i18n::TextId::NczInvalidHeader); return false; }
    const uint64_t sectionCount = readLe64(prefix.data() + 8);
    if (!sectionCount || sectionCount > 64 || sectionCount > (entry.size - kNcaHeaderSize - 16) / kSectionSize) {
        error = i18n::tr(i18n::TextId::NczInvalidSections); return false;
    }
    std::vector<uint8_t> rawSections(static_cast<size_t>(sectionCount) * kSectionSize);
    if (!readNcz(pfs0, entry, kNcaHeaderSize + 16, rawSections.data(), rawSections.size(), error)) return false;
    std::vector<NczSection> sourceSections;
    sourceSections.reserve(static_cast<size_t>(sectionCount));
    for (uint64_t i = 0; i < sectionCount; ++i) {
        const auto* raw = rawSections.data() + static_cast<size_t>(i) * kSectionSize;
        NczSection section;
        section.offset = readLe64(raw);
        section.size = readLe64(raw + 8);
        section.cryptoType = readLe64(raw + 16);
        std::memcpy(section.key.data(), raw + 32, section.key.size());
        std::memcpy(section.counter.data(), raw + 48, section.counter.size());
        if (!section.size || section.offset > std::numeric_limits<uint64_t>::max() - section.size) {
            error = i18n::tr(i18n::TextId::NczInvalidSections); return false;
        }
        sourceSections.push_back(section);
    }

    uint64_t outputOffset = kNcaHeaderSize;
    for (const auto& source : sourceSections) {
        const uint64_t end = source.offset + source.size;
        const uint64_t start = std::max<uint64_t>(source.offset, kNcaHeaderSize);
        if (end <= kNcaHeaderSize) continue;
        if (start > outputOffset && layout.sections.empty()) {
            layout.sections.push_back({outputOffset, start - outputOffset, 1, {}, {}});
            outputOffset = start;
        }
        if (start != outputOffset) { error = i18n::tr(i18n::TextId::NczInvalidSections); return false; }
        NczSection section = source;
        section.offset = start;
        section.size = end - start;
        layout.sections.push_back(section);
        outputOffset = end;
    }
    if (layout.sections.empty()) { error = i18n::tr(i18n::TextId::NczInvalidSections); return false; }
    layout.decompressedSize = outputOffset;
    layout.dataOffset = kNcaHeaderSize + 16 + sectionCount * kSectionSize;
    if (layout.dataOffset >= entry.size) { error = i18n::tr(i18n::TextId::NczInvalidHeader); return false; }

    std::array<uint8_t, 8> magic{};
    if (!readNcz(pfs0, entry, layout.dataOffset, magic.data(), magic.size(), error)) return false;
    if (std::memcmp(magic.data(), "NCZBLOCK", magic.size()) != 0) return true;

    std::array<uint8_t, 24> blockHeader{};
    if (entry.size - layout.dataOffset < blockHeader.size() || !readNcz(pfs0, entry, layout.dataOffset, blockHeader.data(), blockHeader.size(), error)) {
        error = i18n::tr(i18n::TextId::NczInvalidBlock); return false;
    }
    const uint8_t version = blockHeader[8], type = blockHeader[9], exponent = blockHeader[11];
    const uint32_t blockCount = readLe32(blockHeader.data() + 12);
    const uint64_t decompressedSize = readLe64(blockHeader.data() + 16);
    if (version != 2 || type != 1 || exponent < 14 || exponent > 32 || !blockCount ||
        decompressedSize != layout.decompressedSize - kNcaHeaderSize ||
        blockCount > (entry.size - layout.dataOffset - blockHeader.size()) / sizeof(uint32_t)) {
        error = i18n::tr(i18n::TextId::NczInvalidBlock); return false;
    }
    const uint64_t blockSize = uint64_t{1} << exponent;
    const uint64_t expectedBlocks = decompressedSize / blockSize + (decompressedSize % blockSize != 0);
    if (blockCount != expectedBlocks) { error = i18n::tr(i18n::TextId::NczInvalidBlock); return false; }
    std::vector<uint8_t> rawSizes(static_cast<size_t>(blockCount) * sizeof(uint32_t));
    if (!readNcz(pfs0, entry, layout.dataOffset + blockHeader.size(), rawSizes.data(), rawSizes.size(), error)) return false;
    uint64_t compressedTotal{};
    layout.block.compressedSizes.reserve(blockCount);
    for (uint32_t i = 0; i < blockCount; ++i) {
        const uint32_t compressed = readLe32(rawSizes.data() + static_cast<size_t>(i) * sizeof(uint32_t));
        const uint64_t decompressed = std::min<uint64_t>(blockSize, decompressedSize - static_cast<uint64_t>(i) * blockSize);
        if (!compressed || compressed > decompressed || compressedTotal > std::numeric_limits<uint64_t>::max() - compressed) {
            error = i18n::tr(i18n::TextId::NczInvalidBlock); return false;
        }
        compressedTotal += compressed;
        layout.block.compressedSizes.push_back(compressed);
    }
    layout.block.dataOffset = layout.dataOffset + blockHeader.size() + rawSizes.size();
    if (layout.block.dataOffset > entry.size || compressedTotal != entry.size - layout.block.dataOffset) {
        error = i18n::tr(i18n::TextId::NczInvalidBlock); return false;
    }
    layout.block.decompressedSize = decompressedSize;
    layout.block.blockSize = blockSize;
    layout.blockCompressed = true;
    layout.dataOffset = layout.block.dataOffset;
    return true;
}

bool cryptCtr(const NczSection& section, uint64_t offset, uint8_t* data, size_t size, std::string& error) {
    if (section.cryptoType != 3 && section.cryptoType != 4) return true;
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    if (mbedtls_aes_setkey_enc(&aes, section.key.data(), 128) != 0) {
        mbedtls_aes_free(&aes); error = i18n::tr(i18n::TextId::NczDecompressionFailed); return false;
    }
    std::array<uint8_t, 16> nonce{};
    std::memcpy(nonce.data(), section.counter.data(), 8);
    const uint64_t block = offset >> 4;
    for (unsigned i = 0; i < 8; ++i) nonce[8 + i] = static_cast<uint8_t>(block >> ((7 - i) * 8));
    std::array<uint8_t, 16> streamBlock{};
    size_t nonceOffset{};
    const size_t skip = static_cast<size_t>(offset & 0xF);
    if (skip) {
        std::array<uint8_t, 16> zeros{}, ignored{};
        if (mbedtls_aes_crypt_ctr(&aes, skip, &nonceOffset, nonce.data(), streamBlock.data(), zeros.data(), ignored.data()) != 0) {
            mbedtls_aes_free(&aes); error = i18n::tr(i18n::TextId::NczDecompressionFailed); return false;
        }
    }
    const int result = mbedtls_aes_crypt_ctr(&aes, size, &nonceOffset, nonce.data(), streamBlock.data(), data, data);
    mbedtls_aes_free(&aes);
    if (result != 0) { error = i18n::tr(i18n::TextId::NczDecompressionFailed); return false; }
    return true;
}

class NczOutput {
  public:
    NczOutput(const NczLayout& layout, const NczSink& sink) : layout_(layout), sink_(sink) {}

    bool write(uint8_t* data, size_t size, std::string& error) {
        size_t consumed{};
        while (consumed < size) {
            if (section_ >= layout_.sections.size()) { error = i18n::tr(i18n::TextId::NczDecompressionFailed); return false; }
            const auto& current = layout_.sections[section_];
            if (offset_ < current.offset || offset_ >= current.offset + current.size) { error = i18n::tr(i18n::TextId::NczInvalidSections); return false; }
            const size_t amount = static_cast<size_t>(std::min<uint64_t>(size - consumed, current.offset + current.size - offset_));
            if (!cryptCtr(current, offset_, data + consumed, amount, error) || !sink_(offset_, data + consumed, amount, error)) return false;
            offset_ += amount;
            consumed += amount;
            if (offset_ == current.offset + current.size) ++section_;
        }
        return true;
    }

    bool complete() const { return offset_ == layout_.decompressedSize && section_ == layout_.sections.size(); }

  private:
    const NczLayout& layout_;
    const NczSink& sink_;
    size_t section_{};
    uint64_t offset_{kNcaHeaderSize};
};

class ZstdDecoder {
  public:
    ZstdDecoder() : stream_(ZSTD_createDStream()), inputBuffer_(kIoSize), outputBuffer_(kIoSize) {}
    ~ZstdDecoder() { if (stream_) ZSTD_freeDStream(stream_); }
    ZstdDecoder(const ZstdDecoder&) = delete;
    ZstdDecoder& operator=(const ZstdDecoder&) = delete;

    bool decompress(const Pfs0& pfs0, const Pfs0Entry& entry, uint64_t inputOffset, uint64_t inputSize, uint64_t expectedSize, NczOutput& output, std::string& error) {
        if (!stream_ || ZSTD_isError(ZSTD_initDStream(stream_))) { error = i18n::tr(i18n::TextId::NczDecompressionFailed); return false; }
        ZSTD_inBuffer input{inputBuffer_.data(), 0, 0};
        uint64_t loaded{}, produced{};
        size_t remaining = 1;
        bool success = false;
        while (true) {
            if (input.pos == input.size && loaded < inputSize) {
                const size_t amount = static_cast<size_t>(std::min<uint64_t>(inputBuffer_.size(), inputSize - loaded));
                if (!readNcz(pfs0, entry, inputOffset + loaded, inputBuffer_.data(), amount, error)) break;
                loaded += amount;
                input = {inputBuffer_.data(), amount, 0};
            }
            const uint64_t outputRemaining = expectedSize - produced;
            ZSTD_outBuffer decoded{outputBuffer_.data(), static_cast<size_t>(std::min<uint64_t>(outputBuffer_.size(), outputRemaining ? outputRemaining : 1)), 0};
            const size_t before = input.pos;
            remaining = ZSTD_decompressStream(stream_, &decoded, &input);
            if (ZSTD_isError(remaining) || decoded.pos > outputRemaining) break;
            if (decoded.pos && !output.write(outputBuffer_.data(), decoded.pos, error)) return false;
            produced += decoded.pos;
            if (remaining == 0) {
                success = produced == expectedSize && loaded == inputSize && input.pos == input.size;
                break;
            }
            if (decoded.pos == 0 && input.pos == before && loaded == inputSize && input.pos == input.size) break;
        }
        if (!success) { error = i18n::tr(i18n::TextId::NczDecompressionFailed); return false; }
        return true;
    }

  private:
    ZSTD_DStream* stream_{};
    std::vector<uint8_t> inputBuffer_, outputBuffer_;
};

} // namespace

bool inspectNcz(const Pfs0& pfs0, const Pfs0Entry& entry, uint64_t& decompressedSize, std::string& error) {
    NczLayout layout;
    if (!parseNcz(pfs0, entry, layout, error)) return false;
    decompressedSize = layout.decompressedSize;
    return true;
}

bool streamNcz(const Pfs0& pfs0, const Pfs0Entry& entry, const NczSink& sink, std::string& error) {
    if (!sink) { error = i18n::tr(i18n::TextId::NczDecompressionFailed); return false; }
    NczLayout layout;
    if (!parseNcz(pfs0, entry, layout, error)) return false;
    std::vector<uint8_t> header(kNcaHeaderSize);
    if (!readNcz(pfs0, entry, 0, header.data(), header.size(), error) || !sink(0, header.data(), header.size(), error)) return false;
    NczOutput output(layout, sink);
    ZstdDecoder decoder;
    if (!layout.blockCompressed) {
        if (!decoder.decompress(pfs0, entry, layout.dataOffset, entry.size - layout.dataOffset, layout.decompressedSize - kNcaHeaderSize, output, error)) return false;
    } else {
        uint64_t sourceOffset = layout.block.dataOffset;
        uint64_t remainingOutput = layout.block.decompressedSize;
        std::vector<uint8_t> buffer(kIoSize);
        for (const uint32_t compressedSize : layout.block.compressedSizes) {
            const uint64_t blockOutput = std::min<uint64_t>(layout.block.blockSize, remainingOutput);
            if (compressedSize == blockOutput) {
                for (uint64_t offset = 0; offset < blockOutput;) {
                    const size_t amount = static_cast<size_t>(std::min<uint64_t>(buffer.size(), blockOutput - offset));
                    if (!readNcz(pfs0, entry, sourceOffset + offset, buffer.data(), amount, error) || !output.write(buffer.data(), amount, error)) return false;
                    offset += amount;
                }
            } else if (!decoder.decompress(pfs0, entry, sourceOffset, compressedSize, blockOutput, output, error)) return false;
            sourceOffset += compressedSize;
            remainingOutput -= blockOutput;
        }
    }
    if (!output.complete()) { error = i18n::tr(i18n::TextId::NczDecompressionFailed); return false; }
    return true;
}

} // namespace switchdrive
