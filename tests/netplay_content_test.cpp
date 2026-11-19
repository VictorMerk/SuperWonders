#include "netplay_content.h"
#include "mod_overlay.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>

void check(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
int main() {
    namespace fs=std::filesystem;
#if defined(_WIN32)
    const auto library_name="wonder_game.dll";
#else
    const auto library_name="libwonder_game.so";
#endif
    const auto root=fs::temp_directory_path()/("wonder-content-test-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    auto write=[&](const fs::path& p,const char* value){fs::create_directories(p.parent_path());std::ofstream(p)<<value;};
    write(root/"analysis/main.bin","Product120");write(root/"analysis/sdk.bin","SDK16");
    write(root/"game/build-id.txt","kit2");write(root/"game"/library_name,"generated game");write(root/"runtime.exe","runtime");
    auto hash=[&](const char* aspect=""){return nx::lan::content_fingerprint(root,root/"runtime.exe","test",aspect);};
    const auto clean=hash();
    const std::array<unsigned char,32> expected={0x02,0xc2,0x01,0x11,0xcf,0x6b,0xc0,0x1e,0x8b,0xb6,0x18,0x43,0x14,0x8a,0x8c,0x41,0x59,0x09,0x56,0x73,0x82,0x56,0xba,0x9d,0x1c,0x17,0xd8,0xfa,0x9a,0x72,0x10,0x45};
    check(clean==expected,"stable content keeps the documented domain-4 fingerprint format");
    write(root/"mods/-off/romfs/Stage/ignored.bin","disabled");
    write(root/"mods/readme-only/readme.txt","not a mod");
    check(clean==hash(),"disabled and non-loadable mods do not block LAN matching");
    write(root/"mods/-off/romfs/Stage/ignored.bin","changed disabled content");
    check(clean==hash(),"disabled content changes are ignored");
    write(root/"mods/01-first/romfs/Stage/course.bin","first");
    const auto active=hash();check(active!=clean,"active override changes compatibility");
    write(root/"mods/01-first/readme.txt","author notes");
    check(active==hash(),"author notes outside RomFS do not cause false incompatibility");
    write(root/"mods/01-first/romfs/Stage/course.bin","second");
    check(active!=hash(),"active file byte changes are detected");
    write(root/"mods/-00-uw-map-hud-21-9/romfs/camera.bin","wide21");
    write(root/"mods/-00-uw-map-hud-32-9/romfs/camera.bin","wide32");
    check(hash("21:9")!=hash("32:9"),"aspect-selected mods cannot silently match across different profiles");
    const auto wide=hash("21:9");
    write(root/"mods/-00-uw-map-hud-32-9/romfs/camera.bin","inactive profile update");
    check(wide==hash("21:9"),"inactive aspect data is not hashed");
    check(hash()==hash("16:9"),"default and explicit normal aspect match");
    const auto version=hash();write(root/"analysis/main.bin","originalSwitch");
    check(version!=hash(),"different Wonder executables cannot match");
    const auto runtime=hash();write(root/"runtime.exe","different runtime");
    check(runtime!=hash(),"different runtime binaries cannot match");
    const auto dll=hash();write(root/"game"/library_name,"different generated game");
    check(dll!=hash(),"different generated game libraries cannot match");
    write(root/library_name,"adjacent developer library");
    const auto adjacent=hash();write(root/"game"/library_name,"unused data library");
    check(adjacent==hash(),"unused data library cannot override adjacent code identity");
    write(root/library_name,"changed adjacent library");
    check(adjacent!=hash(),"actual adjacent game code is included in LAN matching");
    fs::remove(root/library_name);
    auto override_env=[](const std::string& value) {
#if defined(_WIN32)
        _putenv_s("NX_GAMELIB",value.c_str());
#else
        if(value.empty()) unsetenv("NX_GAMELIB");else setenv("NX_GAMELIB",value.c_str(),1);
#endif
    };
    const std::string inherited=std::getenv("NX_GAMELIB")?std::getenv("NX_GAMELIB"):"";
    const auto before_override=hash();write(root/"override.bin","selected override library");
    override_env((root/"override.bin").string());
    check(before_override!=hash(),"explicit selected library affects compatibility");
    override_env((root/"nonexistent.bin").string());
    check(before_override==hash(),"missing explicit override falls back like the runtime loader");
    override_env(inherited);
    bool cancelled=false;int checkpoints=0;
    try { nx::lan::content_fingerprint(root,root/"runtime.exe","test","",[&] {
        if(++checkpoints==2) throw std::runtime_error("cancelled");
    }); } catch(const std::runtime_error& e) { cancelled=std::string(e.what())=="cancelled"; }
    check(cancelled && checkpoints==2,"preparation cancellation interrupts a file read");
    bool mutated=false;checkpoints=0;
    try { nx::lan::content_fingerprint(root,root/"runtime.exe","test","",[&] {
        if(++checkpoints==2) fs::last_write_time(root/"analysis/main.bin",
            fs::last_write_time(root/"analysis/main.bin")+std::chrono::seconds(2));
    }); } catch(const std::exception&) { mutated=true; }
    check(mutated,"same-size file mutation while hashing fails closed");
    // Trigger a later mutation only after an earlier core file has completed.
    bool late=false;checkpoints=0;
    try { nx::lan::content_fingerprint(root,root/"runtime.exe","test","",[&] {
        if(++checkpoints==8) fs::last_write_time(root/"analysis/main.bin",
            fs::last_write_time(root/"analysis/main.bin")+std::chrono::seconds(2));
    }); } catch(const std::exception&) {late=true;}
    check(late,"an earlier core file changing during later hashing is rejected");
    bool discovery_cancelled=false;
    try { nx::mods::selected(root/"mods","",[]{throw std::runtime_error("discovery cancelled");}); }
    catch(const std::runtime_error& e){discovery_cancelled=std::string(e.what())=="discovery cancelled";}
    check(discovery_cancelled,"mod discovery accepts cancellation before filesystem work");
    // A large later override keeps hashing in progress after the initial manifest
    // has been collected. Add a new file while that later content is read.
    fs::create_directories(root/"mods/05-large/romfs");
    { std::ofstream out(root/"mods/05-large/romfs/large.bin",std::ios::binary);
      const std::string block(16u<<20,'x');out.write(block.data(),block.size()); }
    checkpoints=0;bool added=false,inventory_failed=false;
    try { nx::lan::content_fingerprint(root,root/"runtime.exe","test","",[&] {
        if(++checkpoints==128) {write(root/"mods/01-first/romfs/added.bin","new file");added=true;}
    }); } catch(const std::exception&) {inventory_failed=true;}
    check(added&&inventory_failed,"adding a mod file after enumeration rejects the stale manifest");
    fs::remove_all(root/"mods/05-large");fs::remove(root/"mods/01-first/romfs/added.bin");
    auto deep=root/"mods/04-deep/romfs";
    for(int i=0;i<66;++i) deep/="d";
    fs::create_directories(deep);
    bool depth_failed=false;try{hash();}catch(const std::exception&){depth_failed=true;}
    check(depth_failed,"directory-only deep trees cannot evade the file-count bound");
    fs::remove_all(root/"mods/04-deep");
    fs::remove(root/"analysis/sdk.bin");bool failed=false;
    try{hash();}catch(const std::exception&){failed=true;}
    check(failed,"missing compatibility data fails closed");
    check(root.parent_path()==fs::temp_directory_path()&&root.filename().string().starts_with("wonder-content-test-"),"own cleanup root");
    fs::remove_all(root);
    std::cout<<"LAN content fingerprint tests passed\n";
}
