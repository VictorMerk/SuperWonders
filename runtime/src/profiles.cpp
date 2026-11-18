#include "profiles.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#endif
namespace nx::profiles {
namespace fs = std::filesystem;
namespace {
bool valid_id(const std::string& id) {
    return id == "default" || (id.size() > 2 && id.size() < 64 && id.starts_with("p-") &&
        id.find_first_not_of("p-0123456789") == std::string::npos);
}
bool valid_name(const std::string& name) {
    if (name.empty() || name.size() > 31 || name.front() == ' ' || name.back() == ' ') return false;
    // Check UTF-8, rejecting control characters, overlong sequences and surrogate codepoints.
    for (size_t i=0; i<name.size();) {
        unsigned c = (unsigned char)name[i++], value=c, count=0, minimum=0;
        if(c<32 || c==127) return false;
        if(c>=128) {
            if(c>=0xc2 && c<=0xdf) count=1,value=c&31,minimum=0x80;
            else if(c>=0xe0 && c<=0xef) count=2,value=c&15,minimum=0x800;
            else if(c>=0xf0 && c<=0xf4) count=3,value=c&7,minimum=0x10000;
            else return false;
            while(count--) { if(i==name.size()) return false; c=(unsigned char)name[i++];
                if((c&0xc0)!=0x80) return false; value=(value<<6)|(c&63); }
            if(value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff) || (value>=0x80 && value<0xa0)) return false;
        }
    }
    return true;
}
std::string line(const fs::path& p) { std::ifstream f(p); std::string s; std::getline(f,s); if(!s.empty()&&s.back()=='\r')s.pop_back(); return s; }
bool write(const fs::path& path, const std::string& value, std::string& error) {
    error.clear(); std::error_code ec;
    fs::create_directories(path.parent_path(),ec);
    if(ec) { error=ec.message(); return false; }
    auto temporary=path; temporary += ".tmp";
    { std::ofstream f(temporary,std::ios::binary|std::ios::trunc); f << value << '\n'; f.flush();
      if(!f) { error="Could not save profile settings."; return false; } }
#if defined(_WIN32)
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        error="Could not replace profile settings (Windows error "+std::to_string(GetLastError())+")."; return false;
    }
#else
    fs::rename(temporary,path,ec); if(ec) { error=ec.message(); return false; }
#endif
    return true;
}
fs::path root(const fs::path& data,const std::string& id) { return id=="default" ? data : data/"profiles"/id; }
}
std::vector<Profile> list(const fs::path& data) {
    auto name=line(data/"profile-name.txt");
    std::vector<Profile> result{{"default",valid_name(name)?name:"Player"}};
    std::error_code ec;
    for(fs::directory_iterator it(data/"profiles",ec),end; !ec && it!=end; it.increment(ec)) {
        const auto id=it->path().filename().string();
        if(!valid_id(id) || id=="default" || it->is_symlink(ec) || !it->is_directory(ec)) continue;
        name=line(it->path()/"profile-name.txt"); if(valid_name(name)) result.push_back({id,name});
    }
    std::sort(result.begin()+1,result.end(),[](const auto& a,const auto& b){ return a.id<b.id; });
    return result;
}
Profile active(const fs::path& data) {
    const auto id=line(data/"active-profile.txt");
    const auto all=list(data); for(const auto& p:all) if(p.id==id) return p;
    return all.front();
}
bool select(const fs::path& data,const std::string& id,std::string& error) {
    for(const auto& p:list(data)) if(p.id==id) return write(data/"active-profile.txt",id,error);
    error="Profile no longer exists. Refresh and try again."; return false;
}
bool create(const fs::path& data,const std::string& name,std::string& error) {
    if(!valid_name(name)) { error="Use a name of 1-31 UTF-8 bytes without leading/trailing spaces or control characters."; return false; }
    if(list(data).size()>=16) { error="Up to 16 profiles are supported."; return false; }
    try {
        const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
        fs::create_directories(data/"profiles");
        for(unsigned i=0;i<100;++i) {
            const auto id="p-"+std::to_string(stamp)+"-"+std::to_string(i); const auto r=root(data,id);
            if(!fs::create_directory(r)) continue;
            if(!write(r/"profile-name.txt",name,error)) return false;
            return select(data,id,error);
        }
        error="Could not allocate a profile folder.";
    } catch(const fs::filesystem_error& e) { error=e.code().message(); }
    return false;
}
bool rename(const fs::path& data,const std::string& name,std::string& error) {
    if(!valid_name(name)) { error="Use a name of 1-31 UTF-8 bytes without leading/trailing spaces or control characters."; return false; }
    return write(storage_root(data)/"profile-name.txt",name,error);
}
fs::path storage_root(const fs::path& data) { return root(data,active(data).id); }
fs::path user_root(const fs::path& data) { return storage_root(data)/"user"; }
std::string nickname(const fs::path& data) { return active(data).name; }
}
