#include "netplay_save.h"
#include "launcher/update_crypto.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace nx::lan {
namespace {
namespace fs = std::filesystem;
constexpr size_t max_files = 4096;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
uint64_t get(const uint8_t* p, size_t n) { uint64_t v=0; for(size_t i=0;i<n;++i) v|=uint64_t(p[i])<<(8*i); return v; }
void put(std::vector<uint8_t>& out, uint64_t v, size_t n) { for(size_t i=0;i<n;++i) { out.push_back(uint8_t(v)); v>>=8; } }
std::string normalized_name(std::string name) {
    require(!name.empty() && name.size()<=240,"Invalid session-save path length");
    size_t start=0;
    while(start<name.size()) {
        const auto slash=name.find('/',start);
        const auto part=name.substr(start,slash==std::string::npos?slash:slash-start);
        require(!part.empty() && part!="." && part!=".." && part.back()!='.' && part.back()!=' ',"Unsafe session-save path component");
        for(unsigned char c:part)
            require(c>=32 && c<127 && std::string_view("<>:\\|?*\"").find(char(c))==std::string_view::npos,"Unsupported session-save filename");
        auto upper=part; std::transform(upper.begin(),upper.end(),upper.begin(),[](unsigned char c){return char(std::toupper(c));});
        upper=upper.substr(0,upper.find('.'));
        require(upper!="CON" && upper!="PRN" && upper!="AUX" && upper!="NUL" &&
            !(upper.size()==4 && (upper.starts_with("COM")||upper.starts_with("LPT")) && upper[3]>='1' && upper[3]<='9'),"Reserved session-save filename");
        if(slash==std::string::npos) break;
        start=slash+1; require(start<name.size(),"Session-save paths cannot end in a slash");
    }
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
    return name;
}
bool linked(const fs::path& path) {
#if defined(_WIN32)
    const auto attributes=GetFileAttributesW(path.c_str());
    require(attributes!=INVALID_FILE_ATTRIBUTES,"Could not inspect session-save path");
    return (attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
#else
    return fs::is_symlink(fs::symlink_status(path));
#endif
}
struct Record { std::string name; std::span<const uint8_t> bytes; };
std::vector<Record> parse(std::span<const uint8_t> bytes) {
    require(bytes.size()>=12 && bytes.size()<=kMaxSaveBytes && std::equal(bytes.begin(),bytes.begin()+8,"SWLSAVE1"),"Invalid session-save archive");
    const auto count=get(bytes.data()+8,4); require(count<=max_files,"Too many session-save files");
    std::vector<Record> records; std::set<std::string> names;
    size_t at=12;
    for(size_t i=0;i<count;++i) {
        require(bytes.size()-at>=12,"Truncated session-save record");
        const auto name_size=get(bytes.data()+at,2), reserved=get(bytes.data()+at+2,2), size=get(bytes.data()+at+4,8); at+=12;
        require(!reserved && name_size && name_size<=240 && name_size<=bytes.size()-at,"Invalid session-save file header");
        std::string name(reinterpret_cast<const char*>(bytes.data()+at),size_t(name_size)); at+=size_t(name_size);
        const auto key=normalized_name(name);
        require(names.insert(key).second,"Duplicate session-save filename");
        require(size<=bytes.size()-at,"Truncated session-save payload");
        records.push_back({std::move(name),bytes.subspan(at,size_t(size))}); at+=size_t(size);
    }
    require(at==bytes.size(),"Unexpected session-save trailing data");
    // A file cannot also be another file's parent directory (case insensitive).
    for(const auto& name:names)
        for(auto slash=name.find('/');slash!=std::string::npos;slash=name.find('/',slash+1))
            require(!names.contains(name.substr(0,slash)),"Conflicting session-save file paths");
    return records;
}
}
Fingerprint digest(std::span<const uint8_t> bytes) {
    UcSha256 hash; uc_sha256_init(&hash); uc_sha256_update(&hash,bytes.data(),bytes.size());
    Fingerprint result{}; uc_sha256_final(&hash,result.data()); return result;
}
std::vector<uint8_t> capture_save(const fs::path& save) {
    std::vector<uint8_t> out{'S','W','L','S','A','V','E','1',0,0,0,0};
    std::vector<fs::path> files;
    if(fs::exists(save)) {
        require(!linked(save),"Session-save root cannot be a link");
        for(const auto& entry:fs::recursive_directory_iterator(save)) {
            require(!linked(entry.path()),"Session-save files cannot contain links");
            if(!entry.is_regular_file()) { require(entry.is_directory(),"Unsupported session-save file type"); continue; }
            require(files.size()<max_files,"Too many session-save files"); files.push_back(fs::relative(entry.path(),save));
        }
    }
    std::sort(files.begin(),files.end());
    for(size_t i=0;i<4;++i) out[8+i]=uint8_t(files.size()>>(8*i));
    for(const auto& relative:files) {
        const auto name=relative.generic_string(); normalized_name(name);
        const auto path=save/relative;
        const auto modified=fs::last_write_time(path);
        const auto size=fs::file_size(path);
        require(size<=kMaxSaveBytes && out.size()+12+name.size()<=kMaxSaveBytes-size,"Session save exceeds 64 MiB");
        put(out,name.size(),2); put(out,0,2); put(out,size,8); out.insert(out.end(),name.begin(),name.end());
        const auto from=out.size(); out.resize(from+size_t(size));
        std::ifstream in(path,std::ios::binary); in.read(reinterpret_cast<char*>(out.data()+from),size_t(size));
        require(bool(in) && fs::file_size(path)==size && fs::last_write_time(path)==modified,"Session save changed while being read");
    }
    parse(out); return out;
}
void restore_save(std::span<const uint8_t> bytes,const fs::path& destination) {
    const auto records=parse(bytes);
    require(!fs::exists(destination),"Session-save destination already exists");
    require(fs::is_directory(destination.parent_path()) && !linked(destination.parent_path()),"Session-save parent must be a real directory");
    require(fs::create_directory(destination),"Could not create the session-save destination");
    for(const auto& record:records) {
        const auto path=destination/fs::path(record.name);
        fs::create_directories(path.parent_path());
        std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<const char*>(record.bytes.data()),record.bytes.size()); out.flush();
        require(bool(out),"Writing the session save failed");
    }
}
}
