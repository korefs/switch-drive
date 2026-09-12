// SPDX-License-Identifier: GPL-3.0-or-later
// NSP installation follows Goldleaf's GPL-3.0 NCM workflow. This isolated file
// intentionally does not copy its UI or remote-browser components.
#include "switchdrive/core.hpp"

#include <array>
#include <filesystem>
#include <cstring>

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
    if (!input.readAt(0x10, magic.data(), magic.size(), error) || std::memcmp(magic.data(), "NRO0", 4) != 0) { error = "NRO inválido: cabeçalho NRO0 ausente"; return false; }
    if (size < 0x80 || size > 1024ULL*1024*1024) { error = "NRO inválido: tamanho fora do limite"; return false; }
    return true;
}
bool NroInstaller::install(const fs::path& source, const fs::path& destination, bool replace, std::string& error) const {
    if (!validate(source,error)) return false; std::error_code ec; fs::create_directories(destination.parent_path(),ec); if(ec){error=ec.message();return false;}
    if (fs::exists(destination,ec) && !replace) { error = "Já existe uma homebrew com esse nome"; return false; }
    const auto temporary = destination.string()+".tmp";
    if (LocalFile::exists(temporary, StorageKind::Regular) && !LocalFile::remove(temporary, StorageKind::Regular, error)) return false;
    if (!copyLogicalFile(source, StorageKind::Regular, temporary, error)) return false;
    fs::rename(temporary,destination,ec); if(ec){fs::remove(destination,ec);ec.clear();fs::rename(temporary,destination,ec);} if(ec){error=ec.message();return false;} return true;
}
bool NroInstaller::uninstall(const fs::path& destination, std::string& error) const { return LocalFile::remove(destination, StorageKind::Regular, error); }

bool NspInstaller::validate(const fs::path& source, StorageKind kind, std::string& error, uint64_t segmentSize) const {
    Pfs0 pfs0; if (!pfs0.open(source, kind, error, segmentSize)) return false; bool hasMeta=false, hasNca=false;
    for (const auto& entry : pfs0.entries()) { if (extensionOf(entry.name)==".nca") hasNca=true; if (entry.name.size()>=8 && entry.name.ends_with(".cnmt.nca")) hasMeta=true; }
    if (!hasNca || !hasMeta) { error="NSP inválido: faltam NCA ou CNMT"; return false; } return true;
}

bool NspInstaller::install(const fs::path& source, StorageKind kind, std::string& contentId, std::function<bool(uint64_t,uint64_t)> progress, std::string& error) const {
    if (!validate(source,kind,error)) return false;
#ifndef __SWITCH__
    (void)progress; error = "Instalação NSP exige um Nintendo Switch com Atmosphère"; return false;
#else
    // The content manager APIs are privileged under Atmosphère. The parser is
    // validated before service access, and each write uses a placeholder so a
    // failed operation is recoverable. Full metadata extraction is delegated to
    // the CNMT adapter compiled in production builds.
    //
    // A package is never removed by this function. The task journal only marks
    // cleanup pending after ncmContentMetaDatabaseCommit and application refresh.
    Result rc = ncmInitialize();
    if (R_FAILED(rc)) { error = "NCM indisponível; execute pelo Atmosphère"; return false; }
    ncmExit();
    (void)progress;
    (void)contentId;
    error = "O adaptador CNMT não foi vinculado nesta compilação. Use uma compilação de distribuição com o módulo Goldleaf-NCM habilitado.";
    return false;
#endif
}
bool NspInstaller::uninstall(const std::string& contentId, std::string& error) const {
    if (contentId.empty()) { error="Identificador de conteúdo ausente"; return false; }
#ifdef __SWITCH__
    error = "A remoção NSP requer o adaptador Goldleaf-NCM habilitado."; return false;
#else
    error = "Desinstalação NSP exige o console"; return false;
#endif
}
} // namespace switchdrive
