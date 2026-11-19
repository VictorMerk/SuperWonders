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
using Checkpoint=std::function<void()>;
void number(UcSha256& hash,uint64_t value) {
    std::array<uint8_t,8> bytes{};
    for(auto& byte:bytes) {byte=uint8_t(value);value>>=8;}
    uc_sha256_update(&hash,bytes.data(),bytes.size());
}
void label(UcSha256& hash,std::string_view text) {
    number(hash,text.size());uc_sha256_update(&hash,text.data(),text.size());
}
struct FileState {
    fs::path path;
    uint64_t size;
    fs::file_time_type modified;
    bool operator==(const FileState&) const = default;
};
FileState inspect(const fs::path& path) {
    const auto host=host_path(path);
    const auto size=fs::file_size(host);
    if(size>(1ull<<30)) throw std::runtime_error("LAN compatibility file exceeds the 1 GiB limit");
    return {host,size,fs::last_write_time(host)};
}
void unchanged(const FileState& state) {
    if(state!=inspect(state.path))
        throw std::runtime_error("LAN compatibility files changed during preparation; finish file changes and retry");
}
void file(UcSha256& hash,const FileState& state,const Checkpoint& checkpoint) {
    if(checkpoint) checkpoint();
    unchanged(state);
    number(hash,state.size);
    std::ifstream in(state.path,std::ios::binary);
    if(!in) throw std::runtime_error("Could not read LAN compatibility data");
    std::array<char,65536> block{};
    uint64_t read=0;
    while(in) {
        if(checkpoint) checkpoint();
        in.read(block.data(),block.size());
        const auto count=size_t(in.gcount());
        if(count>state.size-read) throw std::runtime_error("LAN compatibility file grew during preparation; retry after changes finish");
        read+=count;uc_sha256_update(&hash,block.data(),count);
    }
    if(!in.eof() || read!=state.size) throw std::runtime_error("Could not finish reading LAN compatibility data");
    unchanged(state);
}
struct ModState {
    std::string name;
    fs::path romfs;
    std::vector<FileState> files;
    bool operator==(const ModState&) const = default;
};
std::vector<ModState> snapshot(const fs::path& data,std::string_view aspect,const Checkpoint& checkpoint) {
    std::vector<ModState> result;
    uint64_t total=0;size_t count=0,visited=0;
    for(const auto& root:mods::selected(data/"mods",aspect,checkpoint)) {
        if(root.romfs.empty()) continue;
        ModState mod{root.name,host_path(root.romfs),{}};
        for(fs::recursive_directory_iterator it(mod.romfs),end;it!=end;++it) {
            if(checkpoint) checkpoint();
            if(++visited>100000 || it.depth()>64)
                throw std::runtime_error("Active LAN mod tree is too large or deeply nested");
            if(mods::linked(*it)) throw std::runtime_error("Active LAN mods cannot contain nested links");
            if(!it->is_regular_file()) continue;
            auto state=inspect(it->path());
            if(state.size>(1ull<<30)-total || count++>=65536)
                throw std::runtime_error("Active LAN mods exceed the supported compatibility limit");
            total+=state.size;mod.files.push_back(std::move(state));
        }
        std::sort(mod.files.begin(),mod.files.end(),[](const auto& a,const auto& b){return a.path<b.path;});
        result.push_back(std::move(mod));
    }
    return result;
}
}
std::array<unsigned char,32> content_fingerprint(const fs::path& data,const fs::path& runtime,
                                               std::string_view revision,std::string_view aspect,
                                               const Checkpoint& checkpoint) {
    UcSha256 hash;uc_sha256_init(&hash);
    label(hash,"Wonder effective content 4");label(hash,revision);
    label(hash,aspect.empty()?"16:9":aspect);
    std::vector<FileState> core;
    const auto add=[&](const fs::path& path) {
        if(checkpoint) checkpoint();
        core.push_back(inspect(path));file(hash,core.back(),checkpoint);
    };
    for(const char* name:{"analysis/main.bin","analysis/sdk.bin","game/build-id.txt"}) {
        label(hash,name);add(data/name);
    }
    label(hash,"runtime");add(runtime);
#if defined(_WIN32)
    const auto library_name="wonder_game.dll";
#else
    const auto library_name="libwonder_game.so";
#endif
    const auto selected_library=[&] {
        auto library=runtime.parent_path()/library_name;
        if(const auto* override_path=std::getenv("NX_GAMELIB");override_path && *override_path && fs::is_regular_file(host_path(fs::u8path(override_path))))
            library=fs::u8path(override_path);
        else if(!fs::is_regular_file(host_path(library))) library=data/"game"/library_name;
        return host_path(library);
    };
    const auto library=selected_library();
    label(hash,"game/library");add(library);
    const auto mods=snapshot(data,aspect,checkpoint);
    for(const auto& mod:mods) {
        label(hash,"mod");label(hash,mod.name);number(hash,mod.files.size());
        for(const auto& state:mod.files) {
            const auto name=state.path.lexically_relative(mod.romfs).generic_u8string();
            label(hash,{reinterpret_cast<const char*>(name.data()),name.size()});
            file(hash,state,checkpoint);
        }
    }
    // Recheck the complete manifest after hashing: earlier files, selected roots
    // and names must still describe the same content, not just each read's instant.
    if(mods!=snapshot(data,aspect,checkpoint))
        throw std::runtime_error("Active LAN mods changed during preparation; finish file changes and retry");
    for(const auto& state:core) {if(checkpoint) checkpoint();unchanged(state);}
    if(library!=selected_library()) throw std::runtime_error("Selected game code changed during LAN preparation; retry");
    label(hash,"end");
    std::array<unsigned char,32> result{};uc_sha256_final(&hash,result.data());return result;
}
}
