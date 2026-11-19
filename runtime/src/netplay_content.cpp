#include "netplay_content.h"
#include "mod_overlay.h"
#include "host_path.h"
#include "launcher/update_crypto.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace nx::lan {
namespace fs=std::filesystem;
namespace {
void number(UcSha256& hash,uint64_t value) {
    std::array<uint8_t,8> bytes{};
    for(auto& byte:bytes) {byte=uint8_t(value);value>>=8;}
    uc_sha256_update(&hash,bytes.data(),bytes.size());
}
void label(UcSha256& hash,std::string_view text) {
    number(hash,text.size());uc_sha256_update(&hash,text.data(),text.size());
}
void file(UcSha256& hash,const fs::path& path,const std::function<void()>& checkpoint) {
    if(checkpoint) checkpoint();
    const auto size=fs::file_size(host_path(path));
    if(size>(1ull<<30)) throw std::runtime_error("LAN compatibility file exceeds the 1 GiB limit");
    const auto modified=fs::last_write_time(host_path(path));
    number(hash,size);
    std::ifstream in(host_path(path),std::ios::binary);
    if(!in) throw std::runtime_error("Could not read LAN compatibility data");
    std::array<char,65536> block{};
    uint64_t read=0;
    while(in) {
        if(checkpoint) checkpoint();
        in.read(block.data(),block.size());
        const auto count=size_t(in.gcount());
        if(count>size-read) throw std::runtime_error("LAN compatibility file grew during preparation; retry after changes finish");
        read+=count;
        uc_sha256_update(&hash,block.data(),count);
    }
    if(!in.eof() || read!=size || fs::file_size(host_path(path))!=size ||
       fs::last_write_time(host_path(path))!=modified) throw std::runtime_error("LAN compatibility files changed or could not be read");
}
}
std::array<unsigned char,32> content_fingerprint(const fs::path& data,const fs::path& runtime,
                                               std::string_view revision,std::string_view aspect,
                                               const std::function<void()>& checkpoint) {
    UcSha256 hash;uc_sha256_init(&hash);
    label(hash,"Wonder effective content 4");label(hash,revision);
    label(hash,aspect.empty()?"16:9":aspect);
    for(const char* name:{"analysis/main.bin","analysis/sdk.bin","game/build-id.txt"}) {
        label(hash,name);file(hash,data/name,checkpoint);
    }
    label(hash,"runtime");file(hash,runtime,checkpoint);
#if defined(_WIN32)
    const auto library_name="wonder_game.dll";
#else
    const auto library_name="libwonder_game.so";
#endif
    // Match game_lib.cpp's selection order, including a developer's adjacent DLL.
    auto library=runtime.parent_path()/library_name;
    if(const auto* override_path=std::getenv("NX_GAMELIB");override_path && *override_path && fs::is_regular_file(host_path(fs::u8path(override_path))))
        library=fs::u8path(override_path);
    else if(!fs::is_regular_file(host_path(library))) library=data/"game"/library_name;
    label(hash,"game/library");file(hash,library,checkpoint);
    uint64_t total=0;size_t count=0,visited=0;
    const auto roots=mods::selected(data/"mods",aspect);
    for(const auto& root:roots) {
        if(root.romfs.empty()) continue; // The game ignores folders without a usable romfs.
        label(hash,"mod");label(hash,root.name);
        std::vector<fs::path> files;
        for(fs::recursive_directory_iterator it(root.romfs),end;it!=end;++it) {
            if(checkpoint) checkpoint();
            if(++visited>100000 || it.depth()>64)
                throw std::runtime_error("Active LAN mod tree is too large or deeply nested");
            const auto& entry=*it;
            if(mods::linked(entry)) throw std::runtime_error("Active LAN mods cannot contain nested links");
            if(!entry.is_regular_file()) continue;
            const auto size=entry.file_size();
            if(size>(1ull<<30)-total || count++>=65536)
                throw std::runtime_error("Active LAN mods exceed the supported compatibility limit");
            total+=size; files.push_back(entry.path().lexically_relative(root.romfs));
        }
        std::sort(files.begin(),files.end());
        number(hash,files.size());
        for(const auto& relative:files) {
            const auto name=relative.generic_u8string();
            label(hash,{reinterpret_cast<const char*>(name.data()),name.size()});
            file(hash,root.romfs/relative,checkpoint);
        }
    }
    label(hash,"end");
    std::array<unsigned char,32> result{};uc_sha256_final(&hash,result.data());return result;
}
}
