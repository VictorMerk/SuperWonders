// Asset-free CPU filesystem benchmark. This does not measure game FPS.
#include "mod_overlay.h"
#include "host_path.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    namespace fs=std::filesystem;
    using Clock=std::chrono::steady_clock;
    const auto root=fs::temp_directory_path()/("wonder-mod-bench-"+std::to_string(Clock::now().time_since_epoch().count()));
    for(int i=0;i<24;++i) {
        const auto path=root/("mod-"+std::to_string(i))/"romfs"/"synthetic.bin";
        fs::create_directories(path.parent_path());std::ofstream(path)<<"synthetic";
    }
    const auto roots=nx::mods::selected(root);
    const auto begin=Clock::now();const nx::mods::Overlay overlay(roots);const auto built=Clock::now();
    std::vector<std::string> paths;
    for(int i=0;i<1000;++i) paths.push_back("Stage/unmodified-"+std::to_string(i)+".bin");
    size_t reference_hits=0,indexed_hits=0;
    const auto a=Clock::now();
    for(int repeat=0;repeat<4;++repeat) for(const auto& relative:paths)
        for(const auto& mod:roots) {
            std::error_code ec;
            if(fs::is_regular_file(nx::host_path(mod.romfs/fs::u8path(relative)),ec)){++reference_hits;break;}
        }
    const auto b=Clock::now();
    for(int repeat=0;repeat<4;++repeat) for(const auto& relative:paths)
        if(overlay.resolve(relative)) ++indexed_hits;
    const auto c=Clock::now();
    auto us=[](auto duration){return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();};
    std::cout<<"{\"roots\":"<<roots.size()<<",\"lookups\":4000,\"reference_metadata_probes\":96000,"
        <<"\"index_build_us\":"<<us(built-begin)<<",\"reference_lookup_us\":"<<us(b-a)
        <<",\"indexed_lookup_us\":"<<us(c-b)<<",\"answers_match\":"<<(reference_hits==indexed_hits?"true":"false")
        <<",\"scope\":\"synthetic CPU mod-miss lookup; not game FPS or load-time certification\"}\n";
    if(root.parent_path()!=fs::temp_directory_path() || !root.filename().string().starts_with("wonder-mod-bench-")) return 2;
    fs::remove_all(root);
    return reference_hits==indexed_hits?0:1;
}
