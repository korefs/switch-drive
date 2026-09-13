#include "switchdrive/core.hpp"
#include "switchdrive/i18n.hpp"
#include "switchdrive/network.hpp"
#include "switchdrive/qr.hpp"
#include "switchdrive/ui_model.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <zstd.h>

namespace fs = std::filesystem;
using namespace switchdrive;
using namespace switchdrive::i18n;

#pragma pack(push, 1)
struct Header { char magic[4]; uint32_t count, strings, reserved; };
struct Entry { uint64_t offset, size; uint32_t nameOffset, reserved; };
#pragma pack(pop)

void appendLe32(std::vector<uint8_t>& output, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) output.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

void appendLe64(std::vector<uint8_t>& output, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) output.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

std::vector<uint8_t> makeNcz(bool blockCompressed, std::vector<uint8_t>& expected) {
    constexpr size_t headerSize = 0x4000;
    constexpr size_t blockSize = 0x4000;
    expected.resize(headerSize + 25000);
    for (size_t i = 0; i < headerSize; ++i) expected[i] = static_cast<uint8_t>((i * 17) & 0xff);
    for (size_t i = headerSize; i < expected.size(); ++i) expected[i] = static_cast<uint8_t>((i / 97) & 7);
    std::vector<uint8_t> ncz(expected.begin(), expected.begin() + headerSize);
    ncz.insert(ncz.end(), {'N','C','Z','S','E','C','T','N'});
    appendLe64(ncz, 1);
    appendLe64(ncz, headerSize);
    appendLe64(ncz, expected.size() - headerSize);
    appendLe64(ncz, 1);
    appendLe64(ncz, 0);
    ncz.insert(ncz.end(), 32, 0);
    if (!blockCompressed) {
        const size_t bound = ZSTD_compressBound(expected.size() - headerSize);
        const size_t start = ncz.size();
        ncz.resize(start + bound);
        const size_t compressed = ZSTD_compress(ncz.data() + start, bound, expected.data() + headerSize, expected.size() - headerSize, 3);
        assert(!ZSTD_isError(compressed));
        ncz.resize(start + compressed);
        return ncz;
    }
    ncz.insert(ncz.end(), {'N','C','Z','B','L','O','C','K'});
    ncz.push_back(2); ncz.push_back(1); ncz.push_back(0); ncz.push_back(14);
    const uint32_t count = static_cast<uint32_t>((expected.size() - headerSize + blockSize - 1) / blockSize);
    appendLe32(ncz, count);
    appendLe64(ncz, expected.size() - headerSize);
    std::vector<std::vector<uint8_t>> blocks;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t offset = headerSize + static_cast<size_t>(i) * blockSize;
        const size_t amount = std::min(blockSize, expected.size() - offset);
        if (i + 1 == count) {
            blocks.emplace_back(expected.begin() + offset, expected.begin() + offset + amount);
        } else {
            std::vector<uint8_t> compressed(ZSTD_compressBound(amount));
            const size_t size = ZSTD_compress(compressed.data(), compressed.size(), expected.data() + offset, amount, 3);
            assert(!ZSTD_isError(size) && size < amount);
            compressed.resize(size);
            blocks.push_back(std::move(compressed));
        }
    }
    for (const auto& block : blocks) appendLe32(ncz, static_cast<uint32_t>(block.size()));
    for (const auto& block : blocks) ncz.insert(ncz.end(), block.begin(), block.end());
    return ncz;
}

std::vector<uint8_t> makeEncryptedNcz(std::vector<uint8_t>& expected) {
    constexpr size_t headerSize = 0x4000;
    const std::string cipherHex = "8ab827718b97f74b4631898816ea310761948e92d1d1c210dd86ac6248636c8b1ac584d623ba7e2a995a783362024e19e179dcc34145a0d0cc9a572ab29b7300";
    expected.assign(headerSize + cipherHex.size() / 2, 0);
    const auto nibble = [](char value) { return static_cast<uint8_t>(value <= '9' ? value - '0' : value - 'a' + 10); };
    for (size_t i = 0; i < cipherHex.size() / 2; ++i) expected[headerSize + i] = static_cast<uint8_t>((nibble(cipherHex[i * 2]) << 4) | nibble(cipherHex[i * 2 + 1]));
    std::vector<uint8_t> ncz(headerSize, 0);
    ncz.insert(ncz.end(), {'N','C','Z','S','E','C','T','N'});
    appendLe64(ncz, 1);
    appendLe64(ncz, headerSize);
    appendLe64(ncz, cipherHex.size() / 2);
    appendLe64(ncz, 3);
    appendLe64(ncz, 0);
    for (uint8_t i = 0; i < 16; ++i) ncz.push_back(i);
    for (uint8_t i = 0; i < 16; ++i) ncz.push_back(static_cast<uint8_t>(0xa0 + i));
    std::array<uint8_t, 64> plain{};
    for (uint8_t i = 0; i < plain.size(); ++i) plain[i] = i;
    std::vector<uint8_t> compressed(ZSTD_compressBound(plain.size()));
    const size_t size = ZSTD_compress(compressed.data(), compressed.size(), plain.data(), plain.size(), 3);
    assert(!ZSTD_isError(size));
    ncz.insert(ncz.end(), compressed.begin(), compressed.begin() + size);
    return ncz;
}

std::vector<uint8_t> makePfs(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    std::string names;
    std::vector<Entry> entries;
    uint64_t dataOffset{};
    for (const auto& [name, data] : files) {
        entries.push_back({dataOffset, data.size(), static_cast<uint32_t>(names.size()), 0});
        names += name;
        names.push_back('\0');
        dataOffset += data.size();
    }
    Header header{{'P', 'F', 'S', '0'}, static_cast<uint32_t>(files.size()), static_cast<uint32_t>(names.size()), 0};
    std::vector<uint8_t> pfs(sizeof(header) + entries.size() * sizeof(Entry) + names.size());
    size_t cursor{};
    std::memcpy(pfs.data() + cursor, &header, sizeof(header)); cursor += sizeof(header);
    std::memcpy(pfs.data() + cursor, entries.data(), entries.size() * sizeof(Entry)); cursor += entries.size() * sizeof(Entry);
    std::memcpy(pfs.data() + cursor, names.data(), names.size());
    for (const auto& [name, data] : files) { (void)name; pfs.insert(pfs.end(), data.begin(), data.end()); }
    return pfs;
}

class FakeCnmtReader final : public CnmtFileReader {
  public:
    std::vector<std::string> names{"manifest.xml", "Application_0100000000000000.cnmt"};
    std::vector<uint8_t> contents = std::vector<uint8_t>(64, 0xab);
    int64_t reportedSize{64};
    uint32_t failure{0x202};
    int failAt{};
    bool shortRead{}, didRead{};
    std::string openedPath;
    size_t next{};
    uint32_t openDirectory() override { return failAt == 1 ? failure : 0; }
    uint32_t nextFile(std::string& name, bool& end) override {
        if (failAt == 2) return failure;
        end = next == names.size();
        if (!end) name = names[next++];
        return 0;
    }
    uint32_t openFile(const std::string& path) override {
        openedPath = path;
        // Reproduce the FS requirement that the previous code violated.
        if (path.empty() || path.front() != '/') return 0x2ee602;
        return failAt == 3 ? failure : 0;
    }
    uint32_t fileSize(int64_t& size) override {
        size = reportedSize;
        return failAt == 4 ? failure : 0;
    }
    uint32_t readFile(void* data, size_t size, uint64_t& bytesRead) override {
        didRead = true;
        if (failAt == 5) return failure;
        bytesRead = std::min(size, contents.size()) - (shortRead ? 1 : 0);
        std::memcpy(data, contents.data(), static_cast<size_t>(bytesRead));
        return 0;
    }
};

void testCnmtFileReading() {
    std::vector<uint8_t> bytes;
    std::string error;
    FakeCnmtReader valid;
    assert(readCnmtFile(valid, bytes, error));
    assert(valid.openedPath == "/Application_0100000000000000.cnmt");
    assert(bytes == valid.contents && error.empty());

    for (auto language : {Language::EnUs, Language::PtBr, Language::EsEs}) {
        setLanguage(language);
        const std::array stages{TextId::CnmtDirectoryReadFailed, TextId::CnmtDirectoryReadFailed,
            TextId::CnmtFileOpenFailed, TextId::CnmtSizeReadFailed, TextId::CnmtDataReadFailed};
        for (int stage = 1; stage <= 5; ++stage) {
            FakeCnmtReader failing;
            failing.failAt = stage;
            error.clear(); bytes = {0xff};
            assert(!readCnmtFile(failing, bytes, error));
            std::array<char, 256> expected{};
            std::snprintf(expected.data(), expected.size(), tr(stages[stage - 1]), failing.failure);
            assert(error == expected.data() && error.find("0x00000202") != std::string::npos);
            assert(bytes.empty());
        }
        for (const int64_t size : {-1LL, 0LL, 31LL, 16LL * 1024 * 1024 + 1}) {
            FakeCnmtReader invalid;
            invalid.reportedSize = size;
            assert(!readCnmtFile(invalid, bytes, error));
            assert(error == tr(TextId::CnmtInvalidSize) && !invalid.didRead && bytes.empty());
        }
        FakeCnmtReader truncated;
        truncated.shortRead = true;
        assert(!readCnmtFile(truncated, bytes, error));
        assert(error == tr(TextId::CnmtTruncated) && bytes.empty());
        FakeCnmtReader missing;
        missing.names = {"readme.txt", "nested/file.cnmt", "../other.cnmt"};
        assert(!readCnmtFile(missing, bytes, error));
        assert(error == tr(TextId::CnmtFileMissing) && missing.openedPath.empty());
        assert(std::strlen(tr(TextId::InstallFailureUnknown)) > 0);
    }
    setLanguage(Language::EnUs);
}

void testHttpActivity() {
    assert(continueHttpActivity(nullptr));
    ActivityCallback empty;
    assert(continueHttpActivity(&empty));
    int updates = 0;
    bool keepRunning = true;
    ActivityCallback activity = [&] { ++updates; return keepRunning; };
    for (int index = 0; index < 3; ++index) assert(continueHttpActivity(&activity));
    assert(updates == 3); // A nonempty std::function must be invoked, not just tested.
    keepRunning = false;
    assert(!continueHttpActivity(&activity) && updates == 4);
}

void testDownloadRemoval(const fs::path& root) {
    std::string error;
    for (const auto kind : {StorageKind::Regular, StorageKind::Concatenated}) {
        for (const bool installed : {false, true}) {
            StateStore store(root / makeId());
            State state;
            Task task;
            task.id = "download";
            task.displayName = "game.nsz";
            task.localPath = store.downloadPath(task).string();
            task.storageKind = kind;
            task.state = TaskState::Completed;
            state.tasks.push_back(task);
            Task unrelated;
            unrelated.id = "unrelated";
            state.tasks.push_back(unrelated);
            LibraryItem item;
            item.id = task.id;
            item.name = task.displayName;
            item.localPath = task.localPath;
            item.storageKind = kind;
            item.localState = LocalState::Present;
            if (installed) {
                item.installed = InstallKind::Nsp;
                item.nspInstallState = NspInstallState::Installed;
                item.nspMetaId = "0100000000001000";
                item.nspVersion = 42;
                item.installedContentId = "content";
            }
            state.library.push_back(item);
            LocalFile file;
            assert(file.create(task.localPath, kind, error, 8));
            assert(file.writeAt(0, "abcdefghijklmnop", 16, error));
            file.close();
            assert(store.save(state, error));
            assert(store.removeDownload(state, state.library[0].id, error));
            assert(!LocalFile::exists(task.localPath, kind));
            const State loaded = store.load();
            assert(loaded.tasks.size() == 1 && loaded.tasks[0].id == "unrelated");
            assert(loaded.library.size() == (installed ? 1U : 0U));
            if (installed) {
                assert(loaded.library[0].localState == LocalState::Missing);
                assert(loaded.library[0].installed == InstallKind::Nsp);
                assert(loaded.library[0].nspInstallState == NspInstallState::Installed);
                assert(loaded.library[0].nspMetaId == item.nspMetaId && loaded.library[0].nspVersion == 42);
                assert(loaded.library[0].installedContentId == "content");
            }
            // Already missing files can still have their stale download records removed.
            state.library = {item};
            state.library[0].installed = InstallKind::None;
            state.library[0].nspInstallState = NspInstallState::None;
            assert(store.removeDownload(state, state.library[0].id, error));
            assert(state.library.empty());
            state.library = {item};
            state.library[0].installed = InstallKind::None;
            state.library[0].nspInstallState = NspInstallState::Failed;
            assert(store.removeDownload(state, state.library[0].id, error));
            assert(state.library.empty());

            // Wrong targets and filesystem errors must leave records intact.
            state.library = {item};
            state.library[0].localPath = root.string();
            assert(!store.removeDownload(state, item.id, error));
            assert(error == tr(TextId::DownloadRemovalUnsafePath) && state.library.size() == 1 && fs::exists(root));
            state.library = {item};
            state.library[0].storageKind = StorageKind::Regular;
            fs::create_directories(fs::path(item.localPath) / "not-a-file");
            assert(!store.removeDownload(state, item.id, error));
            assert(state.library.size() == 1 && fs::is_directory(item.localPath));

            NspInstallJournal journal;
            journal.operation = "install";
            journal.libraryId = item.id;
            journal.package.metaId = "0100000000001000";
            assert(store.saveInstallJournal(journal, error));
            assert(!store.removeDownload(state, item.id, error));
            assert(error == tr(TextId::DownloadRemovalPending) && state.library.size() == 1);
        }
    }
}

int main() {
    testHttpActivity();
    testCnmtFileReading();
    const auto placeholderSignature = [](const std::string& value) {
        std::string output;
        for (size_t index = 0; index < value.size(); ++index) {
            if (value[index] != '%' || index + 1 == value.size()) continue;
            ++index;
            if (value[index] == '%') continue;
            output += '%';
            while (index + 1 < value.size() && (value[index] == 'l' || value[index] == 'z' || value[index] == 'h')) output += value[index++];
            output += value[index];
        }
        return output;
    };
    std::vector<std::string> english;
    setLanguage(Language::EnUs);
    for (size_t index = 0; index < textCount(); ++index) english.emplace_back(tr(static_cast<TextId>(index)));
    for (const auto language : {Language::EnUs, Language::PtBr, Language::EsEs}) {
        setLanguage(language);
        for (size_t index = 0; index < textCount(); ++index) {
            assert(std::strlen(tr(static_cast<TextId>(index))) > 0);
            assert(placeholderSignature(tr(static_cast<TextId>(index))) == placeholderSignature(english[index]));
        }
    }
    const auto pairingQr = qr::encode("https://drive.example.com/pair/123e4567-e89b-12d3-a456-426614174000/scan/123456");
    assert(pairingQr && pairingQr->size == 37);
    uint32_t qrHash = 2166136261U;
    for (const bool module : pairingQr->modules) { qrHash ^= module; qrHash *= 16777619U; }
    // This vector also verifies finder, alignment, Reed-Solomon, masking, and
    // format information against an independently implemented QR encoder.
    assert(qrHash == 4030055473U);
    assert(!qr::encode(std::string(214, 'a')));
    assert(ui::hitTest({10, 10, 52, 52}, 61, 61) && !ui::hitTest({10, 10, 52, 52}, 62, 62));
    assert(ui::moveSelection(0, 4, -1) == 3 && ui::moveSelection(3, 4, 1) == 0);
    assert(ui::moveSelection(0, 4, -1, false) == 0 && ui::moveSelection(3, 4, 1, false) == 3);
    assert(ui::viewportStart(0, 20, 13) == 0 && ui::viewportStart(12, 20, 13) == 0 && ui::viewportStart(13, 20, 13) == 1 && ui::viewportStart(19, 20, 13) == 7);
    // Touch coordinates use the same row geometry as the renderer, including
    // applet banners, scrolling, row gaps, and out-of-bounds taps.
    assert(ui::touchedRow(300, 170, 0, 20, false) == 0);
    assert(ui::touchedRow(300, 170, 10, 20, false) == 5);
    assert(ui::touchedRow(300, 224, 0, 20, true) == 0);
    assert(ui::touchedRow(300, 170, 0, 20, true) == -1);
    assert(ui::touchedRow(300, 230, 0, 20, false) == -1);
    assert(ui::touchedRow(1240, 170, 0, 20, false) == -1);
    assert(ui::touchedRow(300, 170, 0, 0, false) == -1);
    assert(ui::touchedRow(300, 566, 19, 20, true) == -1);
    // Home/Settings use a two-column grid with an incomplete last row.
    // A must activate whichever card the Joy-Con selected, including X/Y cards.
    ui::MenuFocus focus;
    const std::array<int, 3> cardActions{1, 4, 8};
    assert(focus.activate(3) == 0);
    assert(focus.move(ui::Direction::Right, 3, 0, 4) == -1 && focus.card == 1);
    assert(cardActions[focus.activate(3)] == 4);
    focus.move(ui::Direction::Down, 3, 0, 4);
    assert(focus.card == 2 && cardActions[focus.activate(3)] == 8);
    focus.move(ui::Direction::Right, 3, 0, 4);
    focus.move(ui::Direction::Down, 3, 0, 4);
    assert(focus.card == 2); // No phantom fourth card.
    focus.move(ui::Direction::Up, 3, 0, 4);
    assert(focus.card == 0);
    focus.move(ui::Direction::Left, 3, 0, 4);
    assert(focus.sidebar);
    assert(focus.move(ui::Direction::Up, 3, 0, 4) == 3);
    assert(focus.move(ui::Direction::Down, 3, 3, 4) == 0);
    assert(focus.activate(3) == -1 && !focus.sidebar);
    assert(focus.activate(3) == 0);
    focus.move(ui::Direction::Right, 1, 1, 4);
    focus.move(ui::Direction::Down, 1, 1, 4);
    assert(focus.card == 0 && focus.activate(1) == 0);
    focus.move(ui::Direction::Left, 1, 1, 4);
    focus.move(ui::Direction::Right, 1, 1, 4);
    assert(!focus.sidebar);
    assert(focus.activate(0) == -1);
    focus = {2, false};
    assert(focus.activate(1) == 0); // A smaller page clamps old focus.
    ui::DirectionRepeat repeat;
    assert(repeat.update(1, 1000) == 1);
    assert(repeat.update(1, 1349) == 0);
    assert(repeat.update(1, 1350) == 1);
    assert(repeat.update(1, 1449) == 0);
    assert(repeat.update(1, 1450) == 1);
    assert(repeat.update(2, 1460) == 2); // Direction change responds immediately.
    assert(repeat.update(0, 1470) == 0);
    assert(repeat.update(2, 1480) == 2); // Releasing resets the initial delay.
    assert(repeat.update(2, 1700) == 0);
    for (const auto language : {Language::EnUs, Language::PtBr, Language::EsEs}) {
        setLanguage(language);
        for (const auto id : {TextId::ButtonA, TextId::ButtonX, TextId::ButtonY,
                TextId::Folder, TextId::FileSize, TextId::HomeSubtitle,
                TextId::FilesSubtitle, TextId::LibrarySubtitle,
                TextId::SettingsSubtitle, TextId::NetworkUnavailable,
                TextId::AutoCleanup, TextId::Ellipsis, TextId::ControllerReady,
                TextId::ControllerMissing, TextId::InputUnfocused,
                TextId::AppVersion}) assert(std::strlen(tr(id)) > 0);
    }
    assert(parseLanguage("invalid") == Language::EnUs);
    setLanguage(Language::EnUs);
    assert(std::string(tr(TextId::Files)) == "Files");
    assert(std::string(tr(TextId::Settings)) == "Settings");
    assert(std::string(tr(TextId::ActiveAccount)) == "Active account: %s");
    assert(std::string(tr(TextId::CleanupAfterInstall)) == "Cleanup after install: %s");
    assert(std::string(tr(TextId::NcaMissingDuringInstall)) == "NCA missing during installation");
    assert(std::string(tr(TextId::AppletModeWarning)).find("Application mode") != std::string::npos);
    setLanguage(Language::PtBr);
    assert(std::string(tr(TextId::Files)) == "Arquivos");
    assert(std::string(tr(TextId::Settings)) == "Configurações");
    assert(std::string(tr(TextId::ActiveAccount)) == "Conta ativa: %s");
    assert(std::string(nspContentKindName(NspContentKind::Update)) == "Atualização");
    assert(std::string(nspInstallStorageName(NspInstallStorage::InternalUser)) == "Memória interna");
    setLanguage(Language::EsEs);
    assert(std::string(tr(TextId::Files)) == "Archivos");
    assert(std::string(nspContentKindName(NspContentKind::Update)) == "Actualización");
    assert(std::string(nspInstallStorageName(NspInstallStorage::InternalUser)) == "Memoria interna");
    setLanguage(Language::EnUs);
    assert(formatDataSize(0) == "0 B");
    assert(formatDataSize(1536) == "1.5 KiB");
    assert(formatDataSize(3 * 1024 * 1024 + 512 * 1024) == "3.5 MiB");
    assert(formatDataSize(5ULL * 1024 * 1024 * 1024) == "5.0 GiB");
    assert(formatDuration(9) == "9s" && formatDuration(65) == "1m 05s" && formatDuration(7380) == "2h 03m");
    const auto transferStarted = TransferMeter::Clock::time_point{};
    TransferMeter transferMeter(100, transferStarted);
    TransferEstimate transfer = transferMeter.sample(100, 1100, transferStarted + std::chrono::milliseconds(250));
    assert(!transfer.ready);
    transfer = transferMeter.sample(600, 1100, transferStarted + std::chrono::seconds(1));
    assert(transfer.ready && transfer.bytesPerSecond == 500 && transfer.etaSeconds == 1);
    assert(formatTransferProgress(600, 1100, transfer).find("ETA 1s") != std::string::npos);
    transfer = transferMeter.sample(2100, 10000, transferStarted + std::chrono::seconds(2));
    assert(transfer.ready && transfer.bytesPerSecond == 750 && transfer.etaSeconds == 11);
    assert(sanitizeFileName("../Mario Kart: 8.nsp") == "..Mario_Kart_8.nsp");
    assert(extensionOf("DEMO.NRO") == ".nro");
    assert(isNsp("x.nsp") && !isNsp("x.nsz"));
    assert(isNsz("x.NSZ") && isInstallablePackage("x.nsp") && isInstallablePackage("x.nsz") && !isInstallablePackage("x.xci"));
    assert(storageKindForSize(kFat32FileLimit - 1) == StorageKind::Regular);
    assert(storageKindForSize(kFat32FileLimit) == StorageKind::Concatenated);
    assert(storageKindForSize(kFat32FileLimit + 1) == StorageKind::Concatenated);

    const fs::path root = fs::temp_directory_path() / ("switch-drive-test-" + makeId());
    fs::create_directories(root);
    testDownloadRemoval(root / "removal");
    std::string error;

    const auto regularPath = root / "sequential.bin";
    LocalFile regular;
    assert(regular.create(regularPath, StorageKind::Regular, error));
    assert(regular.writeAt(0, "abcd", 4, error));
    assert(regular.writeAt(4, "efgh", 4, error));
    assert(regular.flush(error));
    uint64_t regularSize{};
    assert(regular.size(regularSize, error) && regularSize == 8);
    regular.close();

    // The host concatenated implementation uses an injectable 8-byte segment
    // size so every operation crosses several physical files.
    const auto logicalPath = root / "large.nsp";
    LocalFile large;
    assert(large.create(logicalPath, StorageKind::Concatenated, error, 8));
    const std::string alphabet = "abcdefghijklmnopqrst";
    assert(large.writeAt(0, alphabet.data(), 11, error));
    assert(large.writeAt(11, alphabet.data() + 11, alphabet.size() - 11, error));
    assert(large.flush(error));
    uint64_t size{};
    assert(large.size(size, error) && size == alphabet.size());
    std::array<char, 10> sample{};
    assert(large.readAt(6, sample.data(), sample.size(), error));
    assert(std::string(sample.data(), sample.size()) == "ghijklmnop");
    assert(large.truncate(13, error));
    assert(large.size(size, error) && size == 13);
    large.close();
    LocalFile reopened;
    assert(reopened.open(logicalPath, StorageKind::Concatenated, true, error, 8));
    assert(reopened.writeAt(13, alphabet.data() + 13, alphabet.size() - 13, error));
    assert(reopened.size(size, error) && size == alphabet.size());
    std::vector<char> restored(alphabet.size());
    assert(reopened.readAt(0, restored.data(), restored.size(), error));
    assert(std::string(restored.data(), restored.size()) == alphabet);

    // A restart truncates any uncommitted tail before the Range request starts.
    assert(reopened.writeAt(alphabet.size(), "tail", 4, error));
    assert(reopened.truncate(alphabet.size(), error));
    assert(reopened.size(size, error) && size == alphabet.size());
    reopened.close();

    StateStore store(root / "state-v2");
    State saved;
    saved.serviceUrl = "https://drive.test";
    saved.sessionToken = "not-a-real-token";
    saved.deleteAfterInstall = false;
    saved.language = "es-ES";
    Task task;
    task.id = "task1";
    task.accountId = "a1";
    task.remoteId = "r1";
    task.displayName = "large.nsp";
    task.localPath = logicalPath.string();
    task.md5 = "d41d8cd98f00b204e9800998ecf8427e";
    task.revision = "42";
    task.etag = "\"etag\"";
    task.expectedSize = kFat32FileLimit + 99;
    task.committedBytes = kFat32FileLimit + 7;
    task.storageKind = StorageKind::Concatenated;
    task.state = TaskState::Paused;
    task.installAfterDownload = true;
    saved.tasks.push_back(task);
    LibraryItem library;
    library.id = "id1";
    library.accountId = "a1";
    library.remoteId = "r1";
    library.name = "a file.nro";
    library.localPath = "/tmp/a file.nro";
    library.md5 = task.md5;
    library.size = 10;
    library.localState = LocalState::Present;
    library.storageKind = StorageKind::Regular;
    saved.library.push_back(library);
    assert(store.save(saved, error));
    saved.sessionToken = "updated-token";
    assert(store.save(saved, error));
    saved.sessionToken = "latest-token";
    assert(store.save(saved, error));
    State loaded = store.load();
    assert(loaded.schemaVersion == 4 && loaded.language == "es-ES" && loaded.sessionToken == "latest-token" && loaded.tasks.size() == 1 && loaded.library.size() == 1);
    assert(loaded.tasks[0].committedBytes == task.committedBytes && loaded.tasks[0].storageKind == StorageKind::Concatenated);
    assert(loaded.tasks[0].revision == "42" && loaded.tasks[0].etag == "\"etag\"");
    {
        std::ifstream backup(root / "state-v2" / "state.json.bak");
        const std::string contents((std::istreambuf_iterator<char>(backup)), {});
        assert(contents.find("updated-token") != std::string::npos && contents.find("latest-token") == std::string::npos);
    }
    for (const char* language : {"en-US", "pt-BR", "es-ES"}) {
        State languageState;
        languageState.language = language;
        StateStore languageStore(root / (std::string("language-") + language));
        assert(languageStore.save(languageState, error));
        assert(languageStore.load().schemaVersion == 4 && languageStore.load().language == language);
    }

    // Existing schema v1 state keeps its catalog entries and adopts regular storage.
    const auto v1Root = root / "state-v1";
    fs::create_directories(v1Root);
    {
        std::ofstream v1(v1Root / "state.json");
        v1 << "{\"schemaVersion\":1,\"serviceUrl\":\"https://drive.test\",\"library\":[{\"id\":\"old\",\"accountId\":\"a\",\"remoteId\":\"r\",\"name\":\"old.nsp\",\"localPath\":\"/tmp/old.nsp\",\"md5\":\"x\"}]}";
    }
    State migrated = StateStore(v1Root).load();
    assert(migrated.schemaVersion == 4 && migrated.language == "en-US" && migrated.library.size() == 1 && migrated.library[0].storageKind == StorageKind::Regular && migrated.library[0].nspInstallState == NspInstallState::None && migrated.tasks.empty());

    const auto v2Root = root / "state-v2-migration";
    fs::create_directories(v2Root);
    { std::ofstream v2(v2Root / "state.json"); v2 << "{\"schemaVersion\":2,\"tasks\":[]}"; }
    assert(StateStore(v2Root).load().schemaVersion == 4 && StateStore(v2Root).load().language == "en-US");

    const auto v3Root = root / "state-v3";
    fs::create_directories(v3Root);
    { std::ofstream v3(v3Root / "state.json"); v3 << "{\"schemaVersion\":3}"; }
    assert(StateStore(v3Root).load().schemaVersion == 4 && StateStore(v3Root).load().language == "en-US");
    const auto invalidLanguageRoot = root / "state-invalid-language";
    fs::create_directories(invalidLanguageRoot);
    { std::ofstream invalid(invalidLanguageRoot / "state.json"); invalid << "{\"schemaVersion\":4,\"language\":\"es-es\"}"; }
    assert(StateStore(invalidLanguageRoot).load().language == "en-US");
    assert(nextLanguage(Language::EnUs) == Language::PtBr && nextLanguage(Language::PtBr) == Language::EsEs && nextLanguage(Language::EsEs) == Language::EnUs);

    NspPackageInfo policy;
    policy.kind = NspContentKind::Update; policy.metaId = "0100000000000800"; policy.version = 12;
    std::vector<InstalledNspInfo> installed{{true, NspInstallStorage::SdCard, 11, policy.metaId, "0100000000000000", NspContentKind::Update}};
    assert(decideNspInstall(policy, installed) == NspInstallDecision::Install);
    policy.version = 11; assert(decideNspInstall(policy, installed) == NspInstallDecision::AlreadyInstalled);
    policy.version = 10; assert(decideNspInstall(policy, installed) == NspInstallDecision::DowngradeBlocked);

    NspInstallJournal savedJournal;
    savedJournal.operation = "install"; savedJournal.libraryId = "id1"; savedJournal.phase = "prepared";
    savedJournal.package = policy; savedJournal.package.metaId = "0100000000000800";
    savedJournal.contents.push_back({"00112233445566778899aabbccddeeff", "ffeeddccbbaa99887766554433221100", true});
    assert(store.saveInstallJournal(savedJournal, error));
    savedJournal.phase = "registered";
    assert(store.saveInstallJournal(savedJournal, error));
    savedJournal.phase = "finalized";
    assert(store.saveInstallJournal(savedJournal, error));
    NspInstallJournal loadedJournal; bool journalExists = false;
    assert(store.loadInstallJournal(loadedJournal, error, journalExists) && journalExists && loadedJournal.phase == "finalized" && loadedJournal.contents.size() == 1 && loadedJournal.package.metaId == savedJournal.package.metaId);
    {
        std::ifstream backup(root / "state-v2" / "install-journal.json.bak");
        const std::string contents((std::istreambuf_iterator<char>(backup)), {});
        assert(contents.find("\"phase\":\"registered\"") != std::string::npos && contents.find("\"phase\":\"finalized\"") == std::string::npos);
    }
    assert(store.clearInstallJournal(error));

    assert(validateRangeResponse(200, "", 0, 100) == RangeResponse::AcceptBody);
    assert(validateRangeResponse(206, "bytes 40-99/100", 40, 100) == RangeResponse::AcceptBody);
    assert(validateRangeResponse(206, "bytes 41-99/100", 40, 100) == RangeResponse::Reject);
    assert(validateRangeResponse(206, "bytes 40-99/101", 40, 100) == RangeResponse::Reject);
    assert(validateRangeResponse(206, "invalid", 40, 100) == RangeResponse::Reject);
    assert(validateRangeResponse(200, "", 40, 100) == RangeResponse::Reject);
    assert(validateRangeResponse(416, "", 100, 100) == RangeResponse::AlreadyComplete);
    assert(validateRangeResponse(416, "", 40, 100) == RangeResponse::Reject);

    // PFS0 reads through the same logical reader across segment boundaries.
    const auto pfsPath = root / "valid.nsp";
    const char names[] = "a.cnmt.nca\0b.nca\0";
    Header header{{'P', 'F', 'S', '0'}, 2, static_cast<uint32_t>(sizeof(names) - 1), 0};
    Entry entries[] = {{0, 4, 0, 0}, {4, 8, 11, 0}};
    std::vector<unsigned char> pfs(sizeof(header) + sizeof(entries) + sizeof(names) - 1 + 12);
    size_t cursor = 0;
    std::memcpy(pfs.data() + cursor, &header, sizeof(header)); cursor += sizeof(header);
    std::memcpy(pfs.data() + cursor, entries, sizeof(entries)); cursor += sizeof(entries);
    std::memcpy(pfs.data() + cursor, names, sizeof(names) - 1); cursor += sizeof(names) - 1;
    std::memcpy(pfs.data() + cursor, "abcdefghijkl", 12);
    LocalFile pfsFile;
    assert(pfsFile.create(pfsPath, StorageKind::Concatenated, error, 16));
    assert(pfsFile.writeAt(0, pfs.data(), pfs.size(), error));
    assert(pfsFile.flush(error));
    Pfs0 parser;
    assert(parser.open(pfsPath, StorageKind::Concatenated, error, 16));
    assert(parser.entries().size() == 2 && parser.entries()[0].name == "a.cnmt.nca");
    NspInstaller nsp;
    assert(nsp.validate(pfsPath, StorageKind::Concatenated, error, 16));

    // NSZ content is reconstructed in memory-sized chunks. Cover both the
    // default solid stream and NCZBLOCK, including a raw final block.
    for (const bool blockCompressed : {false, true}) {
        std::vector<uint8_t> expected;
        const auto ncz = makeNcz(blockCompressed, expected);
        const auto nsz = makePfs({{"content.ncz", ncz}, {"meta.cnmt.nca", {'c','n','m','t'}}});
        const auto nszPath = root / (blockCompressed ? "block.nsz" : "solid.nsz");
        LocalFile nszFile;
        assert(nszFile.create(nszPath, StorageKind::Regular, error));
        assert(nszFile.writeAt(0, nsz.data(), nsz.size(), error));
        assert(nszFile.flush(error));
        nszFile.close();
        Pfs0 nszParser;
        assert(nszParser.open(nszPath, error));
        assert(nsp.validate(nszPath, error));
        const auto* compressed = nszParser.find("content.ncz");
        assert(compressed);
        uint64_t decompressedSize{};
        assert(inspectNcz(nszParser, *compressed, decompressedSize, error));
        assert(decompressedSize == expected.size());
        std::vector<uint8_t> restored(expected.size());
        uint64_t nextOffset{};
        assert(streamNcz(nszParser, *compressed, [&](uint64_t offset, const void* data, size_t size, std::string&) {
            assert(offset == nextOffset && offset + size <= restored.size());
            std::memcpy(restored.data() + offset, data, size);
            nextOffset += size;
            return true;
        }, error));
        assert(nextOffset == expected.size() && restored == expected);
    }
    {
        // The expected cipher text was independently generated with OpenSSL
        // AES-128-CTR using the NCZ absolute-offset counter convention.
        std::vector<uint8_t> expected;
        const auto ncz = makeEncryptedNcz(expected);
        const auto nsz = makePfs({{"encrypted.ncz", ncz}, {"meta.cnmt.nca", {'c','n','m','t'}}});
        const auto nszPath = root / "encrypted.nsz";
        LocalFile nszFile;
        assert(nszFile.create(nszPath, StorageKind::Regular, error));
        assert(nszFile.writeAt(0, nsz.data(), nsz.size(), error));
        assert(nszFile.flush(error));
        nszFile.close();
        Pfs0 nszParser;
        assert(nszParser.open(nszPath, error));
        const auto* compressed = nszParser.find("encrypted.ncz");
        assert(compressed);
        std::vector<uint8_t> restored(expected.size());
        assert(streamNcz(nszParser, *compressed, [&](uint64_t offset, const void* data, size_t size, std::string&) {
            std::memcpy(restored.data() + offset, data, size);
            return true;
        }, error));
        assert(restored == expected);
    }

    // CNMT parsing is host-testable even though opening an encrypted CNMT NCA is Switch-only.
    struct RawHeader { uint64_t id; uint32_t version; uint8_t type, platform; uint16_t ext, count, metaCount; uint8_t attributes, storage, installType, committed; uint32_t required; uint8_t reserved[4]; } __attribute__((packed));
    struct RawContent { uint8_t id[16]; uint32_t low; uint8_t high, attributes, type, offset; } __attribute__((packed));
    struct RawPackaged { uint8_t hash[32]; RawContent info; } __attribute__((packed));
    struct RawPatch { uint64_t applicationId; uint32_t requiredSystem, extendedData; uint8_t reserved[8]; } __attribute__((packed));
    RawHeader rawHeader{0x0100000000000800ULL, 65536, 0x81, 0, static_cast<uint16_t>(sizeof(RawPatch)), 1, 0, 0, 0, 0, 0, 0, {}};
    RawPatch rawPatch{0x0100000000000000ULL, 0, 0, {}};
    RawPackaged rawContent{}; rawContent.info.id[0] = 0xab; rawContent.info.low = 123; rawContent.info.type = 1;
    std::vector<uint8_t> cnmt(sizeof(rawHeader) + sizeof(rawPatch) + sizeof(rawContent));
    std::memcpy(cnmt.data(), &rawHeader, sizeof(rawHeader)); std::memcpy(cnmt.data() + sizeof(rawHeader), &rawPatch, sizeof(rawPatch)); std::memcpy(cnmt.data() + sizeof(rawHeader) + sizeof(rawPatch), &rawContent, sizeof(rawContent));
    NspPackageInfo parsed;
    assert(nsp.parseCnmt(cnmt.data(), cnmt.size(), parsed, error));
    assert(parsed.kind == NspContentKind::Update && parsed.baseTitleId == "0100000000000000" && parsed.metaId == "0100000000000800" && parsed.contents.size() == 1);
    assert(!nsp.parseCnmt(cnmt.data(), sizeof(rawHeader), parsed, error));

    assert(LocalFile::remove(logicalPath, StorageKind::Concatenated, error));
    assert(LocalFile::remove(pfsPath, StorageKind::Concatenated, error));
    fs::remove_all(root);
    std::cout << "core tests passed\n";
}
