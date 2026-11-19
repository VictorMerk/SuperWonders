#include "netplay_save.h"
#include "host_path.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>

static void check(bool ok,const char* message) { if(!ok) { std::fprintf(stderr,"%s\n",message); std::exit(1); } }
template<class F> static bool rejects(F fn) { try { fn(); return false; } catch(...) { return true; } }
static void write(const std::filesystem::path& path,const char* text) {
    std::filesystem::create_directories(path.parent_path()); std::ofstream out(path,std::ios::binary); out<<text;
}
int main() {
    namespace fs=std::filesystem; using namespace nx::lan;
    const auto root=fs::temp_directory_path()/("wonder-lan-save-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    write(root/"source"/"game_data.sav","first save"); write(root/"source"/"nested"/"slot.sav","another slot");
    write(root/"source-assets.bin","never included");
    const auto snapshot=capture_save(root/"source");
    check(snapshot==capture_save(root/"source"),"save encoding is deterministic");
    restore_save(snapshot,root/"restored");
    check(capture_save(root/"restored")==snapshot,"save roundtrip preserves file names and bytes");
    check(rejects([&]{restore_save(snapshot,root/"restored");}),"existing profiles are never overwritten");
    check(!fs::exists(root/"restored"/"source-assets.bin"),"game assets outside the save tree are excluded");
    write(root/"source"/"game_data.sav","changed save");
    check(digest(snapshot)!=digest(capture_save(root/"source")),"save changes alter the session fingerprint");
    for(size_t n=0;n<snapshot.size();++n) check(rejects([&]{restore_save(std::span(snapshot).first(n),root/"truncated");}),"every truncation is rejected before writing");
    check(!fs::exists(root/"truncated"),"invalid saves create no destination");
    const auto malicious=[&](const std::string& name) {
        std::vector<uint8_t> out{'S','W','L','S','A','V','E','1',1,0,0,0};
        out.push_back(uint8_t(name.size())); out.push_back(0);
        for(int i=0;i<10;++i) out.push_back(0);
        out.insert(out.end(),name.begin(),name.end()); return out;
    };
    for(const auto* name: {"../escape","/absolute","C:/escape","a/../../escape","a\\escape","CON","a/NUL.sav","x.","a//b","a/"}) {
        const auto bytes=malicious(name);
        check(rejects([&]{restore_save(bytes,root/"unsafe");}),"unsafe remote filenames are rejected");
    }
    auto duplicate=malicious("same.sav"); duplicate[8]=2;
    const auto entry=malicious("SAME.sav"); duplicate.insert(duplicate.end(),entry.begin()+12,entry.end());
    check(rejects([&]{restore_save(duplicate,root/"duplicate");}),"case-colliding save names are rejected on every platform");
    const auto empty=capture_save(root/"missing"); restore_save(empty,root/"empty");
    check(fs::is_empty(root/"empty"),"a new game can use an empty save");
    int checks=0;
    check(rejects([&]{capture_save(root/"source",[&]{if(++checks==3)throw std::runtime_error("cancelled");});}),
          "host save capture can be cancelled");
    checks=0;
    check(rejects([&]{restore_save(snapshot,root/"cancelled",[&]{if(++checks==3)throw std::runtime_error("cancelled");});}),
          "client restore can be cancelled after creating its destination");
    check(!fs::exists(root/"cancelled"),"cancelled restore removes its partial destination");
    check(capture_save(root/"restored")==snapshot,"rollback preserves unrelated existing saves");
    write(root/"one-file/a.sav","stable");checks=0;
    check(rejects([&]{capture_save(root/"one-file",[&]{
        if(++checks==3)write(root/"one-file/b.sav","added after enumeration");
    });}),"save additions after enumeration cannot produce a stale snapshot");
    fs::create_directories(root/"large");
    {std::ofstream out(root/"large/large.sav",std::ios::binary);const std::string data(256u<<10,'s');out.write(data.data(),data.size());}
    const auto large=capture_save(root/"large");restore_save(large,root/"large-restored");
    check(capture_save(root/"large-restored")==large,"multi-block session saves roundtrip without byte changes");
    checks=0;
    check(rejects([&]{capture_save(root/"large",[&]{if(++checks==6)throw std::runtime_error("cancelled");});}),
          "cancellation interrupts multi-block host reads");
    checks=0;bool partial_written=false;
    check(rejects([&]{restore_save(large,root/"partial-large",[&]{if(++checks==5){
        partial_written=fs::file_size(root/"partial-large/large.sav")>0;throw std::runtime_error("cancelled");
    }});}),"cancellation interrupts multi-block client writes");
    check(partial_written&&!fs::exists(root/"partial-large"),"rollback removes a copy after bytes were written");
    auto deep=root/"deep";
    for(int i=0;i<66;++i)deep/="d";
    fs::create_directories(nx::host_path(deep));
    check(rejects([&]{capture_save(root/"deep");}),"directory-only deep saves are bounded");
    auto long_root=root/fs::path(u"long-\u65e5\u672c\u8a9e");
    for(int i=0;i<5;++i)long_root/=std::string(55,'a'+i);
    fs::create_directories(nx::host_path(long_root));
    restore_save(snapshot,long_root/"restored");
    check(capture_save(long_root/"restored")==snapshot,"long Unicode parent paths preserve nested save restoration");
    check(fs::weakly_canonical(root).parent_path()==fs::weakly_canonical(fs::temp_directory_path()) && root.filename().string().starts_with("wonder-lan-save-test-"),"cleanup is confined to this test directory");
    fs::remove_all(root); std::puts("LAN saves: deterministic roundtrip, isolation, bounds and unsafe-path rejection passed");
}
