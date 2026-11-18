#include "netplay_content.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>

void check(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
int main() {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("wonder-content-test-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    auto write=[&](const fs::path& p,const char* value){fs::create_directories(p.parent_path());std::ofstream(p)<<value;};
    write(root/"analysis/main.bin","Product120");write(root/"analysis/sdk.bin","SDK16");
    write(root/"game/build-id.txt","kit2");write(root/"runtime.exe","runtime");
    auto hash=[&](const char* aspect=""){return nx::lan::content_fingerprint(root,root/"runtime.exe","test",aspect);};
    const auto clean=hash();
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
    fs::remove(root/"analysis/sdk.bin");bool failed=false;
    try{hash();}catch(const std::exception&){failed=true;}
    check(failed,"missing compatibility data fails closed");
    check(root.parent_path()==fs::temp_directory_path()&&root.filename().string().starts_with("wonder-content-test-"),"own cleanup root");
    fs::remove_all(root);
    std::cout<<"LAN content fingerprint tests passed\n";
}
