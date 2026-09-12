#include "switchdrive/core.hpp"
#include "switchdrive/i18n.hpp"
#include "switchdrive/network.hpp"
#include "switchdrive/ui_model.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;
using namespace switchdrive;
using namespace switchdrive::i18n;

#pragma pack(push, 1)
struct Header { char magic[4]; uint32_t count, strings, reserved; };
struct Entry { uint64_t offset, size; uint32_t nameOffset, reserved; };
#pragma pack(pop)

int main() {
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
    assert(sanitizeFileName("../Mario Kart: 8.nsp") == "..Mario_Kart_8.nsp");
    assert(extensionOf("DEMO.NRO") == ".nro");
    assert(isNsp("x.nsp") && !isNsp("x.nsz"));
    assert(storageKindForSize(kFat32FileLimit - 1) == StorageKind::Regular);
    assert(storageKindForSize(kFat32FileLimit) == StorageKind::Concatenated);
    assert(storageKindForSize(kFat32FileLimit + 1) == StorageKind::Concatenated);

    const fs::path root = fs::temp_directory_path() / ("switch-drive-test-" + makeId());
    fs::create_directories(root);
    std::string error;

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
    State loaded = store.load();
    assert(loaded.schemaVersion == 4 && loaded.language == "es-ES" && loaded.tasks.size() == 1 && loaded.library.size() == 1);
    assert(loaded.tasks[0].committedBytes == task.committedBytes && loaded.tasks[0].storageKind == StorageKind::Concatenated);
    assert(loaded.tasks[0].revision == "42" && loaded.tasks[0].etag == "\"etag\"");
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
    NspInstallJournal loadedJournal; bool journalExists = false;
    assert(store.loadInstallJournal(loadedJournal, error, journalExists) && journalExists && loadedJournal.contents.size() == 1 && loadedJournal.package.metaId == savedJournal.package.metaId);
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
