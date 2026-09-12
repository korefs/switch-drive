// SPDX-License-Identifier: GPL-3.0-or-later
// NSP installation follows Goldleaf's GPL-3.0 NCM workflow. This isolated file
// intentionally does not copy its UI or remote-browser components.
#include "switchdrive/core.hpp"
#include "switchdrive/i18n.hpp"

#include <array>
#include <filesystem>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace fs = std::filesystem;
namespace switchdrive {
namespace {

bool copyLogicalFile(const fs::path& source, StorageKind sourceKind, const fs::path& destination, std::string& error) {
    LocalFile input;
    if (!input.open(source, sourceKind, false, error)) return false;
    uint64_t size{};
    if (!input.size(size, error)) return false;
    LocalFile output;
    if (!output.create(destination, StorageKind::Regular, error)) return false;
    std::array<unsigned char, 256 * 1024> buffer{};
    for (uint64_t offset = 0; offset < size;) {
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(buffer.size(), size - offset));
        if (!input.readAt(offset, buffer.data(), chunk, error) || !output.writeAt(offset, buffer.data(), chunk, error)) return false;
        offset += chunk;
    }
    return output.flush(error);
}

} // namespace

bool NroInstaller::validate(const fs::path& source, std::string& error) const {
    LocalFile input;
    uint64_t size{};
    if (!input.open(source, StorageKind::Regular, false, error) || !input.size(size, error)) return false;
    std::array<char, 4> magic{};
    if (!input.readAt(0x10, magic.data(), magic.size(), error) || std::memcmp(magic.data(), "NRO0", 4) != 0) { error = i18n::tr(i18n::TextId::NroInvalidHeader); return false; }
    if (size < 0x80 || size > 1024ULL*1024*1024) { error = i18n::tr(i18n::TextId::NroInvalidSize); return false; }
    return true;
}
bool NroInstaller::install(const fs::path& source, const fs::path& destination, bool replace, std::string& error) const {
    if (!validate(source,error)) return false; std::error_code ec; fs::create_directories(destination.parent_path(),ec); if(ec){error=ec.message();return false;}
    if (fs::exists(destination,ec) && !replace) { error = i18n::tr(i18n::TextId::NroExists); return false; }
    const auto temporary = destination.string()+".tmp";
    if (LocalFile::exists(temporary, StorageKind::Regular) && !LocalFile::remove(temporary, StorageKind::Regular, error)) return false;
    if (!copyLogicalFile(source, StorageKind::Regular, temporary, error)) return false;
    fs::rename(temporary,destination,ec); if(ec){fs::remove(destination,ec);ec.clear();fs::rename(temporary,destination,ec);} if(ec){error=ec.message();return false;} return true;
}
bool NroInstaller::uninstall(const fs::path& destination, std::string& error) const { return LocalFile::remove(destination, StorageKind::Regular, error); }

bool NspInstaller::validate(const fs::path& source, StorageKind kind, std::string& error, uint64_t segmentSize) const {
    Pfs0 pfs0; if (!pfs0.open(source, kind, error, segmentSize)) return false;
    size_t metadataCount = 0; bool hasNca = false;
    for (const auto& entry : pfs0.entries()) {
        if (extensionOf(entry.name) == ".nca") hasNca = true;
        if (entry.name.ends_with(".cnmt.nca")) ++metadataCount;
    }
    if (!hasNca || metadataCount != 1) { error = i18n::tr(metadataCount > 1 ? i18n::TextId::NspAmbiguous : i18n::TextId::NspMissingContents); return false; }
    return true;
}

namespace {
#pragma pack(push, 1)
struct PackagedMetaHeader { uint64_t id; uint32_t version; uint8_t type, platform; uint16_t extendedSize, contentCount, contentMetaCount; uint8_t attributes, storage, installType, committed; uint32_t requiredDownloadSystem; uint8_t reserved[4]; };
struct ContentInfoRaw { uint8_t id[16]; uint32_t sizeLow; uint8_t sizeHigh, attributes, type, idOffset; };
struct PackagedContentInfoRaw { uint8_t hash[32]; ContentInfoRaw info; };
struct ApplicationExtra { uint64_t patchId; uint32_t requiredSystem, requiredApplication; };
struct PatchExtra { uint64_t applicationId; uint32_t requiredSystem, extendedDataSize; uint8_t reserved[8]; };
struct AddOnExtra { uint64_t applicationId; uint32_t requiredApplication; uint8_t accessibilities, padding[3]; uint64_t dataPatchId; };
struct LegacyAddOnExtra { uint64_t applicationId; uint32_t requiredApplication, padding; };
#pragma pack(pop)
static_assert(sizeof(PackagedMetaHeader) == 0x20);
static_assert(sizeof(ContentInfoRaw) == 0x18);

std::string hexId(const uint8_t* value, size_t size) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; ++i) out << std::setw(2) << static_cast<unsigned>(value[i]);
    return out.str();
}
std::string hexTitle(uint64_t value) {
    std::ostringstream out; out << std::uppercase << std::hex << std::setfill('0') << std::setw(16) << value; return out.str();
}
bool hexToBytes(const std::string& value, uint8_t* output, size_t size) {
    if (value.size() != size * 2) return false;
    for (size_t i = 0; i < size; ++i) {
        const auto piece = value.substr(i * 2, 2); char* end = nullptr;
        const auto parsed = std::strtoul(piece.c_str(), &end, 16);
        if (!end || *end) return false; output[i] = static_cast<uint8_t>(parsed);
    }
    return true;
}
uint64_t contentSize(const ContentInfoRaw& content) { return (static_cast<uint64_t>(content.sizeHigh) << 32) | content.sizeLow; }
bool isNcaFileName(const std::string& name, const std::string& id, bool meta) { return name == id + (meta ? ".cnmt.nca" : ".nca"); }
#ifdef __SWITCH__
bool importPackageTicket(const Pfs0& pfs0, NspInstallJournal& journal, std::string& error) {
    const Pfs0Entry* ticket = nullptr; const Pfs0Entry* certificate = nullptr;
    for (const auto& entry : pfs0.entries()) { if (extensionOf(entry.name) == ".tik") ticket = &entry; if (extensionOf(entry.name) == ".cert") certificate = &entry; }
    if (!ticket) return true;
    // Goldleaf imports tickets through the privileged es service. Refuse a
    // ticketed package without its certificate instead of guessing a fallback.
    if (!certificate) { error = i18n::tr(i18n::TextId::NspTicketCertificateMissing); return false; }
    std::vector<uint8_t> ticketBytes(ticket->size), certificateBytes(certificate->size);
    if (!pfs0.read(*ticket, 0, ticketBytes.data(), ticketBytes.size(), error) || !pfs0.read(*certificate, 0, certificateBytes.data(), certificateBytes.size(), error)) return false;
    Service es{}; Result rc = smGetService(&es, "es");
    if (R_SUCCEEDED(rc)) rc = serviceDispatch(&es, 1, .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In, SfBufferAttr_HipcMapAlias | SfBufferAttr_In }, .buffers = { { ticketBytes.data(), ticketBytes.size() }, { certificateBytes.data(), certificateBytes.size() } });
    serviceClose(&es);
    if (R_FAILED(rc)) { error = i18n::tr(i18n::TextId::NspTicketImportFailed); return false; }
    journal.ticketImported = true;
    return true;
}
#endif
}

bool NspInstaller::parseCnmt(const void* raw, size_t size, NspPackageInfo& out, std::string& error) const {
    out = {};
    if (!raw || size < sizeof(PackagedMetaHeader)) { error = i18n::tr(i18n::TextId::CnmtTruncated); return false; }
    const auto* bytes = static_cast<const uint8_t*>(raw);
    const auto& header = *reinterpret_cast<const PackagedMetaHeader*>(bytes);
    const uint64_t entriesStart = sizeof(header) + header.extendedSize;
    const uint64_t entriesSize = static_cast<uint64_t>(header.contentCount) * sizeof(PackagedContentInfoRaw);
    if (header.contentCount == 0 || header.contentCount > 4096 || entriesStart > size || entriesSize > size - entriesStart) { error = i18n::tr(i18n::TextId::CnmtInvalidContents); return false; }
    out.metaId = hexTitle(header.id); out.version = header.version; out.attributes = header.attributes;
    out.extendedHeader.assign(bytes + sizeof(header), bytes + entriesStart);
    switch (header.type) {
        case 0x80:
            if (header.extendedSize != sizeof(ApplicationExtra)) { error = i18n::tr(i18n::TextId::CnmtInvalidBase); return false; }
            out.kind = NspContentKind::BaseGame; out.baseTitleId = out.metaId;
            out.requiredSystemVersion = reinterpret_cast<const ApplicationExtra*>(out.extendedHeader.data())->requiredSystem;
            out.requiredApplicationVersion = reinterpret_cast<const ApplicationExtra*>(out.extendedHeader.data())->requiredApplication;
            break;
        case 0x81:
            if (header.extendedSize != sizeof(PatchExtra)) { error = i18n::tr(i18n::TextId::CnmtInvalidUpdate); return false; }
            out.kind = NspContentKind::Update;
            { const auto* extra = reinterpret_cast<const PatchExtra*>(out.extendedHeader.data()); out.baseTitleId = hexTitle(extra->applicationId); out.requiredSystemVersion = extra->requiredSystem; }
            break;
        case 0x82:
            out.kind = NspContentKind::Dlc;
            if (header.extendedSize == sizeof(AddOnExtra)) { const auto* extra = reinterpret_cast<const AddOnExtra*>(out.extendedHeader.data()); out.baseTitleId = hexTitle(extra->applicationId); out.requiredApplicationVersion = extra->requiredApplication; }
            else if (header.extendedSize == sizeof(LegacyAddOnExtra)) { const auto* extra = reinterpret_cast<const LegacyAddOnExtra*>(out.extendedHeader.data()); out.baseTitleId = hexTitle(extra->applicationId); out.requiredApplicationVersion = extra->requiredApplication; }
            else { error = i18n::tr(i18n::TextId::CnmtInvalidDlc); return false; }
            break;
        default: error = i18n::tr(i18n::TextId::CnmtUnsupported); return false;
    }
    for (uint16_t i = 0; i < header.contentCount; ++i) {
        const auto& entry = *reinterpret_cast<const PackagedContentInfoRaw*>(bytes + entriesStart + static_cast<size_t>(i) * sizeof(PackagedContentInfoRaw));
        if (entry.info.type == 6) continue; // Delta fragments are not installable here.
        NspContentEntry content{hexId(entry.info.id, sizeof(entry.info.id)), contentSize(entry.info), entry.info.type};
        if (!content.size) { error = i18n::tr(i18n::TextId::CnmtEmptyContent); return false; }
        out.totalInstallBytes += content.size;
        out.contents.push_back(std::move(content));
    }
    if (out.contents.empty()) { error = i18n::tr(i18n::TextId::CnmtNoInstallableContent); return false; }
    return true;
}

bool NspInstaller::inspect(const fs::path& source, StorageKind kind, NspPackageInfo& info, std::string& error, uint64_t segmentSize) const {
    if (!validate(source, kind, error, segmentSize)) return false;
    Pfs0 pfs0; if (!pfs0.open(source, kind, error, segmentSize)) return false;
    const Pfs0Entry* meta = nullptr;
    for (const auto& entry : pfs0.entries()) if (entry.name.ends_with(".cnmt.nca")) meta = &entry;
    if (!meta) { error = i18n::tr(i18n::TextId::CnmtMissing); return false; }
#ifndef __SWITCH__
    (void)pfs0; (void)meta;
    error = i18n::tr(i18n::TextId::CnmtSwitchOnly);
    return false;
#else
    // Goldleaf mounts the CNMT NCA through FS after staging it in SystemContent.
    // The dedicated temporary filename prevents package metadata from being mixed.
    FsFileSystem systemFs{};
    Result rc = fsOpenBisFileSystem(&systemFs, FsBisPartitionId_System, "");
    if (R_FAILED(rc) || fsdevMountDevice("swd-system", systemFs) != 0) { error = i18n::tr(i18n::TextId::SystemContentMountFailed); return false; }
    const fs::path temporary = "swd-system:/Contents/switch-drive-cnmt.nca";
    std::FILE* output = std::fopen(temporary.string().c_str(), "wb");
    if (!output) { fsdevUnmountDevice("swd-system"); error = i18n::tr(i18n::TextId::CnmtPrepareFailed); return false; }
    std::array<uint8_t, 256 * 1024> buffer{};
    bool copied = true;
    for (uint64_t offset = 0; offset < meta->size;) {
        const size_t amount = static_cast<size_t>(std::min<uint64_t>(buffer.size(), meta->size - offset));
        if (!pfs0.read(*meta, offset, buffer.data(), amount, error) || std::fwrite(buffer.data(), 1, amount, output) != amount) { copied = false; break; }
        offset += amount;
    }
    std::fclose(output);
    if (!copied) { std::remove(temporary.string().c_str()); fsdevUnmountDevice("swd-system"); return false; }
    char contentPath[FS_MAX_PATH]{}; std::snprintf(contentPath, sizeof(contentPath), "@SystemContent://switch-drive-cnmt.nca");
    FsRightsId rights{}; uint8_t keyGeneration{};
    rc = fsGetRightsIdAndKeyGenerationByPath(contentPath, FsContentAttributes_All, &keyGeneration, &rights);
    FsFileSystem cnmtFs{};
    if (R_SUCCEEDED(rc)) rc = fsOpenFileSystemWithId(&cnmtFs, 0, FsFileSystemType_ContentMeta, contentPath, FsContentAttributes_All);
    if (R_FAILED(rc)) { std::remove(temporary.string().c_str()); fsdevUnmountDevice("swd-system"); error = i18n::tr(i18n::TextId::CnmtOpenFailed); return false; }
    FsDir dir{}; rc = fsFsOpenDirectory(&cnmtFs, "/", FsDirOpenMode_ReadFiles, &dir);
    FsDirectoryEntry entry{}; s64 count{}; std::string cnmtName;
    if (R_SUCCEEDED(rc)) { fsDirRead(&dir, &count, 1, &entry); fsDirClose(&dir); if (count == 1) cnmtName = entry.name; }
    if (cnmtName.empty()) { fsFsClose(&cnmtFs); std::remove(temporary.string().c_str()); fsdevUnmountDevice("swd-system"); error = i18n::tr(i18n::TextId::CnmtFileMissing); return false; }
    FsFile file{}; s64 cnmtSize{}; rc = fsFsOpenFile(&cnmtFs, cnmtName.c_str(), FsOpenMode_Read, &file);
    if (R_SUCCEEDED(rc)) rc = fsFileGetSize(&file, &cnmtSize);
    std::vector<uint8_t> bytes(cnmtSize > 0 ? static_cast<size_t>(cnmtSize) : 0);
    if (R_SUCCEEDED(rc) && !bytes.empty()) rc = fsFileRead(&file, 0, bytes.data(), bytes.size(), FsReadOption_None, nullptr);
    fsFileClose(&file); fsFsClose(&cnmtFs); std::remove(temporary.string().c_str()); fsdevCommitDevice("swd-system"); fsdevUnmountDevice("swd-system");
    if (R_FAILED(rc) || !parseCnmt(bytes.data(), bytes.size(), info, error)) return false;
    info.keyGeneration = keyGeneration;
    info.metaNcaId = meta->name.substr(0, meta->name.size() - std::string(".cnmt.nca").size());
    info.hasTicket = std::any_of(pfs0.entries().begin(), pfs0.entries().end(), [](const Pfs0Entry& entry) { return extensionOf(entry.name) == ".tik"; });
    for (const auto& content : info.contents) {
        const auto it = std::find_if(pfs0.entries().begin(), pfs0.entries().end(), [&](const Pfs0Entry& candidate) { return isNcaFileName(candidate.name, content.id, false); });
        if (it == pfs0.entries().end() || it->size != content.size) { error = i18n::tr(i18n::TextId::NspContentMissing); return false; }
    }
    return true;
#endif
}

bool NspInstaller::queryInstalled(const NspPackageInfo& package, std::vector<InstalledNspInfo>& installed, std::string& error) const {
    installed.clear();
#ifndef __SWITCH__
    (void)package; error = i18n::tr(i18n::TextId::NspQuerySwitchOnly); return false;
#else
    Result rc = ncmInitialize();
    if (R_FAILED(rc)) { error = i18n::tr(i18n::TextId::NcmUnavailable); return false; }
    rc = nsInitialize();
    if (R_FAILED(rc)) { ncmExit(); error = i18n::tr(i18n::TextId::NsUnavailable); return false; }
    uint64_t base{}, metadataId{}; std::stringstream baseStream; baseStream << std::hex << package.baseTitleId; baseStream >> base;
    std::stringstream metadataStream; metadataStream << std::hex << package.metaId; metadataStream >> metadataId;
    std::array<NsApplicationContentMetaStatus, 32> statuses{}; s32 count{};
    rc = nsListApplicationContentMetaStatus(base, 0, statuses.data(), statuses.size(), &count);
    if (R_SUCCEEDED(rc)) for (s32 i = 0; i < count; ++i) {
        const auto& status = statuses[static_cast<size_t>(i)];
        if (status.storageID == NcmStorageId_GameCard || status.application_id != metadataId) continue;
        if (status.meta_type != (package.kind == NspContentKind::BaseGame ? NcmContentMetaType_Application : package.kind == NspContentKind::Update ? NcmContentMetaType_Patch : NcmContentMetaType_AddOnContent)) continue;
        InstalledNspInfo item; item.present = true; item.storage = status.storageID == NcmStorageId_BuiltInUser ? NspInstallStorage::InternalUser : NspInstallStorage::SdCard;
        item.version = status.version; item.metaId = package.metaId; item.baseTitleId = package.baseTitleId; item.kind = package.kind; installed.push_back(std::move(item));
    }
    nsExit(); ncmExit();
    if (R_FAILED(rc)) { error = i18n::tr(i18n::TextId::InstalledQueryFailed); return false; }
    return true;
#endif
}

bool NspInstaller::install(const fs::path& source, StorageKind kind, const NspPackageInfo& package, NspInstallStorage destination, StateStore& store, NspInstallJournal& journal, std::function<bool(uint64_t,uint64_t)> progress, std::string& error) const {
#ifndef __SWITCH__
    (void)source; (void)kind; (void)package; (void)destination; (void)store; (void)journal; (void)progress;
    error = i18n::tr(i18n::TextId::NspInstallSwitchOnly); return false;
#else
    std::vector<InstalledNspInfo> installed;
    if (!queryInstalled(package, installed, error)) return false;
    const auto decision = decideNspInstall(package, installed);
    if (decision == NspInstallDecision::DowngradeBlocked) { error = i18n::tr(i18n::TextId::DowngradeBlocked); return false; }
    if (decision == NspInstallDecision::AlreadyInstalled) return true;
    Pfs0 pfs0; if (!pfs0.open(source, kind, error)) return false;
    const auto* metaEntry = pfs0.find(package.metaNcaId + ".cnmt.nca");
    if (!metaEntry) { error = i18n::tr(i18n::TextId::MetadataNcaMissing); return false; }
    Result rc = ncmInitialize(); if (R_FAILED(rc)) { error = i18n::tr(i18n::TextId::NcmUnavailable); return false; }
    NcmContentStorage contentStorage{}; NcmContentMetaDatabase database{};
    const NcmStorageId storageId = destination == NspInstallStorage::InternalUser ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard;
    rc = ncmOpenContentStorage(&contentStorage, storageId); if (R_SUCCEEDED(rc)) rc = ncmOpenContentMetaDatabase(&database, storageId);
    if (R_FAILED(rc)) { ncmContentStorageClose(&contentStorage); ncmExit(); error = i18n::tr(i18n::TextId::InstallDestinationOpenFailed); return false; }
    int64_t freeSpace{}; rc = ncmContentStorageGetFreeSpaceSize(&contentStorage, &freeSpace);
    if (R_FAILED(rc) || freeSpace < static_cast<int64_t>(package.totalInstallBytes + metaEntry->size)) { ncmContentMetaDatabaseClose(&database); ncmContentStorageClose(&contentStorage); ncmExit(); error = i18n::tr(i18n::TextId::DestinationNoSpace); return false; }
    journal.operation = "install"; journal.phase = "prepared"; journal.package = package; journal.targetStorage = destination; journal.previous = installed;
    journal.contents.clear();
    std::vector<NspContentEntry> all = package.contents;
    all.push_back({package.metaNcaId, metaEntry->size, static_cast<uint8_t>(NcmContentType_Meta)});
    for (const auto& item : all) {
        uint8_t rawId[16]{}; if (!hexToBytes(item.id, rawId, sizeof(rawId))) { error = i18n::tr(i18n::TextId::InvalidNcaId); goto fail; }
        NcmContentId id{}; std::memcpy(id.c, rawId, sizeof(rawId)); bool exists{};
        if (R_FAILED(ncmContentStorageHas(&contentStorage, &exists, &id))) { error = i18n::tr(i18n::TextId::NcaQueryFailed); goto fail; }
        NspJournalContent entry; entry.id = item.id; entry.created = !exists;
        if (!exists) { NcmPlaceHolderId placeholder{}; if (R_FAILED(ncmContentStorageGeneratePlaceHolderId(&contentStorage, &placeholder))) { error = i18n::tr(i18n::TextId::PlaceholderCreateFailed); goto fail; } entry.placeholderId = hexId(placeholder.uuid.uuid, sizeof(placeholder.uuid.uuid)); }
        journal.contents.push_back(std::move(entry));
    }
    if (!store.saveInstallJournal(journal, error)) goto fail;
    appletLockExit();
    {
        uint64_t total = 0, written = 0; for (const auto& item : all) total += item.size;
        std::array<uint8_t, 256 * 1024> buffer{};
        for (size_t index = 0; index < all.size(); ++index) {
            const auto& item = all[index]; auto& itemJournal = journal.contents[index]; if (!itemJournal.created) { written += item.size; continue; }
            uint8_t rawId[16]{}, rawPlaceholder[16]{}; hexToBytes(item.id, rawId, sizeof(rawId)); hexToBytes(itemJournal.placeholderId, rawPlaceholder, sizeof(rawPlaceholder));
            NcmContentId id{}; NcmPlaceHolderId placeholder{}; std::memcpy(id.c, rawId, sizeof(rawId)); std::memcpy(placeholder.uuid.uuid, rawPlaceholder, sizeof(rawPlaceholder));
            if (R_FAILED(ncmContentStorageCreatePlaceHolder(&contentStorage, &id, &placeholder, item.size))) { error = i18n::tr(i18n::TextId::NcaReserveFailed); goto rollback; }
            const Pfs0Entry* packageEntry = pfs0.find(item.id + (item.type == NcmContentType_Meta ? ".cnmt.nca" : ".nca"));
            if (!packageEntry || packageEntry->size != item.size) { error = i18n::tr(i18n::TextId::NspContentMissing); goto rollback; }
            for (uint64_t offset = 0; offset < item.size;) {
                const size_t amount = static_cast<size_t>(std::min<uint64_t>(buffer.size(), item.size - offset));
                if (!pfs0.read(*packageEntry, offset, buffer.data(), amount, error) || R_FAILED(ncmContentStorageWritePlaceHolder(&contentStorage, &placeholder, offset, buffer.data(), amount))) { error = i18n::tr(i18n::TextId::NcaWriteFailed); goto rollback; }
                offset += amount; written += amount;
                if (!progress(written, total)) { error = i18n::tr(i18n::TextId::InstallCancelled); goto rollback; }
            }
            if (R_FAILED(ncmContentStorageFlushPlaceHolder(&contentStorage)) || R_FAILED(ncmContentStorageRegister(&contentStorage, &id, &placeholder))) { error = i18n::tr(i18n::TextId::NcaRegisterFailed); goto rollback; }
            ncmContentStorageDeletePlaceHolder(&contentStorage, &placeholder);
            journal.phase = "registered"; if (!store.saveInstallJournal(journal, error)) goto rollback;
        }
    }
    if (package.hasTicket) {
        if (!store.saveInstallJournal(journal, error) || !importPackageTicket(pfs0, journal, error) || !store.saveInstallJournal(journal, error)) goto rollback;
    }
    {
        uint64_t title{}; std::stringstream titleStream; titleStream << std::hex << package.metaId; titleStream >> title;
        NcmContentMetaKey key{}; key.id = title; key.version = package.version; key.type = package.kind == NspContentKind::BaseGame ? NcmContentMetaType_Application : package.kind == NspContentKind::Update ? NcmContentMetaType_Patch : NcmContentMetaType_AddOnContent; key.install_type = NcmContentInstallType_Full;
        std::vector<uint8_t> metadata(sizeof(NcmContentMetaHeader) + package.extendedHeader.size() + (all.size()) * sizeof(NcmContentInfo));
        auto* header = reinterpret_cast<NcmContentMetaHeader*>(metadata.data()); header->extended_header_size = package.extendedHeader.size(); header->content_count = all.size(); header->attributes = package.attributes; header->storage_id = storageId;
        std::memcpy(metadata.data() + sizeof(*header), package.extendedHeader.data(), package.extendedHeader.size());
        auto* infos = reinterpret_cast<NcmContentInfo*>(metadata.data() + sizeof(*header) + package.extendedHeader.size());
        for (size_t i = 0; i < all.size(); ++i) { hexToBytes(all[i].id, infos[i].content_id.c, sizeof(infos[i].content_id.c)); ncmU64ToContentInfoSize(all[i].size, &infos[i]); infos[i].content_type = all[i].type; }
        if (R_FAILED(ncmContentMetaDatabaseSet(&database, &key, metadata.data(), metadata.size())) || R_FAILED(ncmContentMetaDatabaseCommit(&database))) { error = i18n::tr(i18n::TextId::MetadataCommitFailed); goto rollback; }
        journal.phase = "committed"; if (!store.saveInstallJournal(journal, error)) { error = i18n::tr(i18n::TextId::JournalUpdateAfterInstallFailed); }
    }
    ncmContentMetaDatabaseClose(&database); ncmContentStorageClose(&contentStorage); ncmExit(); appletUnlockExit();
    return error.empty();
rollback:
    for (const auto& item : journal.contents) if (item.created && !item.placeholderId.empty()) { uint8_t raw[16]{}; if (hexToBytes(item.placeholderId, raw, sizeof(raw))) { NcmPlaceHolderId holder{}; std::memcpy(holder.uuid.uuid, raw, sizeof(raw)); ncmContentStorageDeletePlaceHolder(&contentStorage, &holder); } }
    journal.phase = "rollback"; store.saveInstallJournal(journal, error);
fail:
    ncmContentMetaDatabaseClose(&database); ncmContentStorageClose(&contentStorage); ncmExit(); appletUnlockExit(); return false;
#endif
}

bool NspInstaller::recover(StateStore& store, NspInstallJournal& journal, std::string& error) const {
#ifndef __SWITCH__
    (void)store; (void)journal; error = i18n::tr(i18n::TextId::RecoverySwitchOnly); return false;
#else
    // A committed metadata key is authoritative. Journal phases are only hints:
    // an interrupted state write must never turn a committed install into a rollback.
    if (journal.operation.empty()) return true;
    bool committed = journal.operation == "remove";
    if (journal.operation == "install") {
        Result rc = ncmInitialize(); if (R_FAILED(rc)) { error = i18n::tr(i18n::TextId::RecoveryNcmUnavailable); return false; }
        NcmContentMetaDatabase database{}; const auto storage = journal.targetStorage == NspInstallStorage::InternalUser ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard;
        rc = ncmOpenContentMetaDatabase(&database, storage);
        uint64_t title{}; std::stringstream stream; stream << std::hex << journal.package.metaId; stream >> title;
        NcmContentMetaKey key{}; key.id = title; key.version = journal.package.version; key.type = journal.package.kind == NspContentKind::BaseGame ? NcmContentMetaType_Application : journal.package.kind == NspContentKind::Update ? NcmContentMetaType_Patch : NcmContentMetaType_AddOnContent; key.install_type = NcmContentInstallType_Full;
        if (R_SUCCEEDED(rc)) rc = ncmContentMetaDatabaseHas(&database, &committed, &key);
        ncmContentMetaDatabaseClose(&database); ncmExit();
        if (R_FAILED(rc)) { error = i18n::tr(i18n::TextId::RecoveryCommitCheckFailed); return false; }
    }
    if (journal.operation == "install" && !committed) {
        Result rc = ncmInitialize(); if (R_FAILED(rc)) { error = i18n::tr(i18n::TextId::RecoveryNcmUnavailable); return false; }
        NcmContentStorage storage{}; const auto id = journal.targetStorage == NspInstallStorage::InternalUser ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard;
        rc = ncmOpenContentStorage(&storage, id); if (R_SUCCEEDED(rc)) for (const auto& item : journal.contents) if (item.created && !item.placeholderId.empty()) { uint8_t bytes[16]{}; if (hexToBytes(item.placeholderId, bytes, sizeof(bytes))) { NcmPlaceHolderId holder{}; std::memcpy(holder.uuid.uuid, bytes, sizeof(bytes)); ncmContentStorageDeletePlaceHolder(&storage, &holder); } }
        ncmContentStorageClose(&storage); ncmExit();
    }
    if (!store.clearInstallJournal(error)) return false;
    journal = {}; return true;
#endif
}

bool NspInstaller::uninstall(const InstalledNspInfo& target, StateStore& store, NspInstallJournal& journal, std::string& error) const {
#ifndef __SWITCH__
    (void)target; (void)store; (void)journal; error = i18n::tr(i18n::TextId::UninstallSwitchOnly); return false;
#else
    if (!target.present || target.metaId.empty()) { error = i18n::tr(i18n::TextId::ManagedContentMissing); return false; }
    Result rc = ncmInitialize(); if (R_FAILED(rc)) { error = i18n::tr(i18n::TextId::NcmUnavailable); return false; }
    const auto storageId = target.storage == NspInstallStorage::InternalUser ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard;
    NcmContentMetaDatabase database{}; rc = ncmOpenContentMetaDatabase(&database, storageId); if (R_FAILED(rc)) { ncmExit(); error = i18n::tr(i18n::TextId::MetadataOpenFailed); return false; }
    uint64_t title{}; std::stringstream titleStream; titleStream << std::hex << target.metaId; titleStream >> title;
    NcmContentMetaKey key{}; key.id = title; key.version = target.version; key.type = target.kind == NspContentKind::BaseGame ? NcmContentMetaType_Application : target.kind == NspContentKind::Update ? NcmContentMetaType_Patch : NcmContentMetaType_AddOnContent; key.install_type = NcmContentInstallType_Full;
    bool exists{}; if (R_FAILED(ncmContentMetaDatabaseHas(&database, &exists, &key)) || !exists) { ncmContentMetaDatabaseClose(&database); ncmExit(); error = i18n::tr(i18n::TextId::ManagedVersionMissing); return false; }
    journal = {}; journal.operation = "remove"; journal.phase = "prepared"; journal.package.metaId = target.metaId; journal.package.baseTitleId = target.baseTitleId; journal.package.version = target.version; journal.package.kind = target.kind; journal.targetStorage = target.storage;
    std::array<NcmContentInfo, 128> oldContents{}; s32 written{};
    if (R_FAILED(ncmContentMetaDatabaseListContentInfo(&database, &written, oldContents.data(), oldContents.size(), &key, 0))) { ncmContentMetaDatabaseClose(&database); ncmExit(); error = i18n::tr(i18n::TextId::ContentEnumerateFailed); return false; }
    for (s32 i = 0; i < written; ++i) journal.contents.push_back({hexId(oldContents[static_cast<size_t>(i)].content_id.c, sizeof(oldContents[static_cast<size_t>(i)].content_id.c)), {}, true});
    if (!store.saveInstallJournal(journal, error)) { ncmContentMetaDatabaseClose(&database); ncmExit(); return false; }
    appletLockExit(); rc = ncmContentMetaDatabaseRemove(&database, &key); if (R_SUCCEEDED(rc)) rc = ncmContentMetaDatabaseCommit(&database);
    ncmContentMetaDatabaseClose(&database); ncmExit(); appletUnlockExit();
    if (R_FAILED(rc)) { error = i18n::tr(i18n::TextId::MetadataRemovalFailed); return false; }
    journal.phase = "committed"; if (!store.saveInstallJournal(journal, error)) return false;
    NcmContentStorage contentStorage{}; NcmContentMetaDatabase cleanupDatabase{}; rc = ncmInitialize(); if (R_SUCCEEDED(rc)) rc = ncmOpenContentStorage(&contentStorage, storageId); if (R_SUCCEEDED(rc)) rc = ncmOpenContentMetaDatabase(&cleanupDatabase, storageId);
    if (R_SUCCEEDED(rc)) {
        for (const auto& content : journal.contents) {
            uint8_t bytes[16]{}; if (!hexToBytes(content.id, bytes, sizeof(bytes))) continue;
            NcmContentId id{}; std::memcpy(id.c, bytes, sizeof(bytes)); bool orphan{};
            if (R_SUCCEEDED(ncmContentMetaDatabaseLookupOrphanContent(&cleanupDatabase, &orphan, &id, 1)) && orphan) ncmContentStorageDelete(&contentStorage, &id);
        }
        ncmContentMetaDatabaseClose(&cleanupDatabase);
        ncmContentStorageClose(&contentStorage);
    }
    ncmExit();
    return store.clearInstallJournal(error);
#endif
}

bool NspInstaller::install(const fs::path& source, StorageKind kind, std::string& contentId, std::function<bool(uint64_t,uint64_t)> progress, std::string& error) const {
    NspPackageInfo package; if (!inspect(source, kind, package, error)) return false;
    StateStore store("sdmc:/switch-drive"); NspInstallJournal journal; journal.localPath = source.string();
    const bool result = install(source, kind, package, NspInstallStorage::SdCard, store, journal, std::move(progress), error);
    if (result) contentId = package.metaId;
    return result;
}
bool NspInstaller::uninstall(const std::string& contentId, std::string& error) const {
    InstalledNspInfo target; target.present = true; target.metaId = contentId; target.kind = NspContentKind::BaseGame;
    StateStore store("sdmc:/switch-drive"); NspInstallJournal journal;
    return uninstall(target, store, journal, error);
}
} // namespace switchdrive
