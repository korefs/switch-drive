#include "switchdrive/core.hpp"

#include <cassert>
#include <chrono>
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

    const fs::path root = fs::temp_directory_path() / ("switch-drive-test-" + makeId());
    fs::create_directories(root);
    StateStore store(root);
    State saved; saved.serviceUrl="https://drive.test"; saved.sessionToken="not-a-real-token"; saved.deleteAfterInstall=false;
    saved.library.push_back({"id1","a1","r1","a file.nro","/tmp/a file.nro","d41d8cd98f00b204e9800998ecf8427e",10,LocalState::Present,InstallKind::None,"",""});
    std::string error; assert(store.save(saved,error)); State loaded=store.load();
    assert(loaded.serviceUrl==saved.serviceUrl && loaded.sessionToken==saved.sessionToken && !loaded.deleteAfterInstall && loaded.library.size()==1 && loaded.library[0].name=="a file.nro");

    const auto pfs = root / "valid.nsp";
    Header header{{'P','F','S','0'},2,22,0};
    Entry entries[]={{0,4,0,0},{4,8,10,0}}; const char names[]="0123456789.cnmt.nca\0x.nca\0";
    { std::ofstream out(pfs,std::ios::binary); out.write(reinterpret_cast<const char*>(&header),sizeof(header)); out.write(reinterpret_cast<const char*>(entries),sizeof(entries)); out.write(names,sizeof(names)-1); out.write("abcdefghijkl",12); }
    Pfs0 parser; assert(parser.open(pfs,error)); assert(parser.entries().size()==2); assert(parser.entries()[0].name=="0123456789.cnmt.nca");
    NspInstaller nsp; assert(nsp.validate(pfs,error));

    const auto bad = root / "bad.nsp"; { std::ofstream out(bad,std::ios::binary); out << "nope"; } assert(!parser.open(bad,error));
    fs::remove_all(root);
    std::cout << "core tests passed\n";
}
