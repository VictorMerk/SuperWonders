#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include "platform.h"
namespace nx::overlay {
struct Usability {
    bool reduced_motion=false, menu_fullscreen=false, auto_backup=true;
    int menu_opacity=100;
};
inline Usability load_usability(const std::filesystem::path& data) {
    Usability s; std::ifstream f(data/"usability.ini"); std::string line;
    while(std::getline(f,line)) {
        const auto at=line.find('='); if(at==std::string::npos)continue;
        auto key=line.substr(0,at),v=line.substr(at+1); if(!v.empty()&&v.back()=='\r')v.pop_back();
        if(key=="reduced_motion") s.reduced_motion=v=="1";
        if(key=="menu_fullscreen") s.menu_fullscreen=v=="1";
        if(key=="auto_backup") s.auto_backup=v!="0";
        if(key=="menu_opacity") { try {s.menu_opacity=std::clamp(std::stoi(v),75,100);}catch(...){} }
    } return s;
}
inline bool save_usability(const std::filesystem::path& data,const Usability& s) {
    std::error_code ec; std::filesystem::create_directories(data,ec);
    std::ofstream f(data/"usability.ini");
    f<<"reduced_motion="<<s.reduced_motion<<"\nmenu_fullscreen="<<s.menu_fullscreen
     <<"\nauto_backup="<<s.auto_backup<<"\nmenu_opacity="<<s.menu_opacity<<'\n'; return bool(f);
}
inline Usability& usability_preferences() {
    static Usability settings=load_usability(std::filesystem::u8path(plat::data_root())); return settings;
}
inline bool persist_usability() {return save_usability(std::filesystem::u8path(plat::data_root()),usability_preferences());}
}
