#include "mod_overlay.h"
#include "host_path.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>

void check(bool ok,const char* message) {if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
int main() {
    namespace fs=std::filesystem;
    using namespace nx::mods;
    const auto root=fs::temp_directory_path()/("wonder-overlay-test-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    auto write=[&](const fs::path& p){fs::create_directories(p.parent_path());std::ofstream(p)<<"test";};
    write(root/"01-first/romfs/Model/shared.bin");
    write(root/"02-second/atmosphere/contents/010015100B514000/romfs/Model/shared.bin");
    write(root/"02-second/atmosphere/contents/0000000000000001/romfs/wrong.bin");
    write(root/"-00-off/romfs/disabled.bin");
    write(root/"-00-uw-map-hud-21-9/romfs/camera.bin");
    write(root/"00-uw-map-hud-32-9/romfs/camera.bin");
    write(root/"03-other-title/contents/0000000000000001/romfs/wrong.bin");
    write(root/"01-first/romfs"/fs::u8path(u8"日本語.bin"));
    check(enabled("ordinary")&&!enabled("-ordinary"),"ordinary enable/disable");
    check(enabled("-00-uw-map-hud-21-9","21:9")&&!enabled("00-uw-map-hud-32-9","21:9"),
          "aspect profile selects its own roots even with disabled folder prefix");
    check(find_romfs(root/"03-other-title").empty(),"another game's title is not applied to Wonder");
    check(find_romfs(root/"02-second").generic_string().find("010015100B514000")!=std::string::npos,
          "multi-title bundle selects Wonder");
    auto roots=selected(root,"16:9");
    Overlay overlay(roots);
    auto hit=overlay.resolve("Model/shared.bin");
    check(hit&&hit->generic_string().find("01-first")!=std::string::npos,"first mod wins");
    check(!overlay.resolve("missing.bin")&&!overlay.resolve("disabled.bin"),"absent/disabled files fall through to base");
    check(overlay.resolve(std::string(reinterpret_cast<const char*>(u8"日本語.bin"))).has_value(),"Unicode files retain lookup semantics");
    fs::remove(root/"01-first/romfs/Model/shared.bin");
    hit=overlay.resolve("Model/shared.bin");
    check(hit&&hit->generic_string().find("02-second")!=std::string::npos,"removed winner falls through to next mod");
    write(root/"02-second/atmosphere/contents/010015100B514000/romfs/new.bin");
    check(!overlay.resolve("new.bin"),"file-name snapshot remains stable until restart");
    Overlay refreshed(selected(root,"16:9"));
    check(refreshed.resolve("new.bin").has_value(),"new files load after rescan");
    Overlay bounded(selected(root,"16:9"),1);
    check(bounded.fallback_roots()>0&&bounded.resolve("new.bin").has_value(),"index limit preserves filesystem fallback");
    std::error_code ec;
    fs::create_directory_symlink(root/"02-second/atmosphere/contents/010015100B514000/romfs",root/"01-first/romfs/linked",ec);
    if(!ec) {
        Overlay links(selected(root,"16:9"));
        check(links.fallback_roots()>0&&links.resolve("linked/new.bin").has_value(),"linked directories keep old lookup behavior without recursive scanning");
    }
    check(!bounded.resolve("../outside.bin")&&!bounded.resolve("/absolute.bin")&&
          !bounded.resolve("C:/absolute.bin")&&!bounded.resolve(std::string("bad\0name",8)),
          "fallback never resolves invalid or non-relative guest names");
    // Exercise the merge of indexed candidates and a preceding fallback root.
    Overlay all_fallback(selected(root,"16:9"),0);
    check(all_fallback.resolve("new.bin").has_value(),"all-fallback lookup retains later roots");
    auto deep=root/"04-deep/romfs";
    for(int i=0;i<66;++i) deep/="d";
    write(nx::host_path(deep/"deep.bin"));
    Overlay deep_index(selected(root,"16:9"));
    check(deep_index.fallback_roots()>0,"excessively deep overlays use filesystem fallback");
    save_order(root,{"02-second","01-first"});
    Overlay reordered(selected(root,"16:9"));
    hit=reordered.resolve("Model/shared.bin");
    check(hit&&hit->generic_string().find("02-second")!=std::string::npos,"custom order is used by runtime lookup");
    // Recreate the first winner removed by the earlier fallback test.
    write(root/"01-first/romfs/Model/shared.bin");save_order(root,{"01-first","02-second"});
    check(Overlay(selected(root,"16:9")).resolve("Model/shared.bin")->generic_string().find("01-first")!=std::string::npos,"custom priorities preserve first-mod-wins semantics");
    std::string order_error;
    check(set_all_enabled(root,false,order_error)&&!fs::exists(root/"01-first")&&fs::exists(root/"-01-first"),"ordinary mods can be disabled together");
    check(fs::exists(root/"00-uw-map-hud-32-9"),"bulk selection preserves aspect-controlled folders");
    check(set_all_enabled(root,true,order_error)&&fs::exists(root/"01-first"),"ordinary mods can be enabled together without renaming their logical order");
    fs::create_directories(root/"-01-first/romfs");
    check(!set_all_enabled(root,false,order_error)&&fs::exists(root/"02-second")&&fs::exists(root/"01-first"),"duplicate folder preflight prevents partial bulk changes");
    fs::remove_all(root/"-01-first");
    int move_checks=0;
    check(!set_all_enabled(root,false,order_error,[&]{if(++move_checks==2)throw std::runtime_error("Interrupted before a later rename");})&&
          move_checks==2&&fs::exists(root/"01-first")&&fs::exists(root/"02-second"),
          "interruption after an earlier rename rolls the transaction back");

    bool bad_order=false;try{save_order(root,{"../escape"});}catch(const std::exception&){bad_order=true;}
    check(bad_order,"custom order cannot name a parent path");save_order(root,{});
    auto long_root=root;
    for(int i=0;i<5;++i) long_root/=std::string(55,'a'+i);
    const auto long_file=nx::host_path(long_root/"mod/romfs/long.bin");
    write(long_file);
    Overlay long_paths(selected(long_root));
    check(long_paths.resolve("long.bin").has_value(),"long Windows paths retain mod overrides");
    check(root.parent_path()==fs::temp_directory_path()&&root.filename().string().starts_with("wonder-overlay-test-"),"own cleanup root");
    fs::remove_all(nx::host_path(root));
    std::cout<<"mod overlay tests passed\n";
}
