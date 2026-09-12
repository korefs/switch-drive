#include "switchdrive/core.hpp"
#include "switchdrive/network.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;
using namespace switchdrive;

#pragma pack(push, 1)
struct Header { char magic[4]; uint32_t count, strings, reserved; };
struct Entry { uint64_t offset, size; uint32_t nameOffset, reserved; };
#pragma pack(pop)

int main() {
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
    assert(loaded.schemaVersion == 3 && loaded.tasks.size() == 1 && loaded.library.size() == 1);
    assert(loaded.tasks[0].committedBytes == task.committedBytes && loaded.tasks[0].storageKind == StorageKind::Concatenated);
    assert(loaded.tasks[0].revision == "42" && loaded.tasks[0].etag == "\"etag\"");

    // Existing schema v1 state keeps its catalog entries and adopts regular storage.
    const auto v1Root = root / "state-v1";
    fs::create_directories(v1Root);
    {
        std::ofstream v1(v1Root / "state.json");
        v1 << "{\"schemaVersion\":1,\"serviceUrl\":\"https://drive.test\",\"library\":[{\"id\":\"old\",\"accountId\":\"a\",\"remoteId\":\"r\",\"name\":\"old.nsp\",\"localPath\":\"/tmp/old.nsp\",\"md5\":\"x\"}]}";
    }
    State migrated = StateStore(v1Root).load();
    assert(migrated.schemaVersion == 3 && migrated.library.size() == 1 && migrated.library[0].storageKind == StorageKind::Regular && migrated.library[0].nspInstallState == NspInstallState::None && migrated.tasks.empty());

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
