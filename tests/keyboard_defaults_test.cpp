#include "keyboard_defaults.h"
#include "overlay/settings.h"
#if defined(_WIN32)
#include "pc_menu_win.h"
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
static void check(bool value,const char* message) {
    if(!value){std::fprintf(stderr,"%s\n",message);std::exit(1);}
}
int main() {
    using namespace nx::input;
    using namespace nx::overlay;
    for(auto codes:{KeyboardCodes::Windows,KeyboardCodes::SDL}) {
        const auto keys=recommended_keyboard_bindings(codes);
        const int space=codes==KeyboardCodes::Windows?0x20:44,shift=codes==KeyboardCodes::Windows?0x10:225;
        check(keys[CtlB]==space && keys[CtlY]==shift,"Space jumps and Shift runs on both platforms");
        check(keys[CtlA]!=shift && keys[CtlB]!=shift,"Shift cannot trigger either jump action");
        for(size_t i=0;i<keys.size();++i)for(size_t j=0;j<i;++j)check(keys[i]==0 || keys[i]!=keys[j],"bound default keys have no action collisions");
        auto first_pc=version_one_keyboard_bindings(codes);
        check(migrate_keyboard_defaults(first_pc,1,codes)&&first_pc==keys,"version-one PC preset migrates to correct semantic actions");
        auto old=legacy_keyboard_bindings(codes);
        check(migrate_keyboard_defaults(old,0,codes)&&old==keys,"untouched legacy preset migrates");
        auto custom=legacy_keyboard_bindings(codes);custom[CtlL]=200;
        const auto preserved=custom;
        check(!migrate_keyboard_defaults(custom,0,codes)&&custom==preserved,"one custom key preserves the whole layout");
        auto deliberate=legacy_keyboard_bindings(codes);
        check(!migrate_keyboard_defaults(deliberate,1,codes)&&deliberate==legacy_keyboard_bindings(codes),"versioned custom legacy layout stays intact");
    }
    auto right_shift=[](int code){return code==229;};auto right_ctrl=[](int code){return code==228;};
    check(keyboard_key_held(225,KeyboardCodes::SDL,right_shift),"right Shift activates the Shift binding");
    check(keyboard_key_held(224,KeyboardCodes::SDL,right_ctrl),"right Ctrl activates the Ctrl binding");
    check(!keyboard_key_held(229,KeyboardCodes::SDL,[](int code){return code==225;}),"explicit right-only custom binding stays right-only");
#if defined(_WIN32)
    check(nx::plat::PcMenuState{}.keyboard_bindings==Settings{}.keyboard_bindings,"native Windows and overlay defaults agree");
    check(Settings{}.keyboard_bindings[CtlY]==VK_SHIFT&&Settings{}.keyboard_bindings[CtlZL]==VK_CONTROL,"Win32 modifier codes match SDK");
#endif
    const auto path=std::filesystem::temp_directory_path()/("wonder-keyboard-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ini");
    const auto filename=path.string();
    auto write=[&](const KeyboardBindings& keys,int version) {
        std::ofstream f(path);f<<"volume=73\n";
        if(version>=0)f<<"keyboard_layout_version="<<version<<'\n';
        for(size_t i=0;i<keys.size();++i)f<<"keyboard_"<<i<<'='<<keys[i]<<'\n';
    };
    auto old=legacy_keyboard_bindings(native_keyboard_codes());
    old[CtlMinus]=native_keyboard_codes()==KeyboardCodes::Windows?27:41;
    write(old,-1);Settings loaded;
    check(load_settings(filename,loaded)&&loaded.keyboard_bindings==recommended_keyboard_bindings()&&loaded.volume==73,"real old settings migrate without resetting other options");
    save_settings(filename,loaded);std::ifstream f(path);std::stringstream content;content<<f.rdbuf();f.close();
    check(content.str().find("keyboard_layout_version=2")!=std::string::npos,"layout version is persisted");
    auto custom=legacy_keyboard_bindings(native_keyboard_codes());custom[CtlL]=200;write(custom,-1);
    check(load_settings(filename,loaded)&&loaded.keyboard_bindings==custom,"custom file is preserved");
    auto explicit_old=legacy_keyboard_bindings(native_keyboard_codes());write(explicit_old,1);
    check(load_settings(filename,loaded)&&loaded.keyboard_bindings==explicit_old,"versioned legacy file is preserved");
    std::filesystem::remove(path);
    std::puts("keyboard action semantics, modifier aliases, default migration and custom-binding preservation passed");
}
