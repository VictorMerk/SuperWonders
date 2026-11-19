#include "netplay_save.h"
#include "host_path.h"
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
struct SaveFile {
    fs::path relative;
    uint64_t size;
    fs::file_time_type modified;
    bool operator==(const SaveFile&) const = default;
};
using Checkpoint=std::function<void()>;
std::vector<SaveFile> scan(const fs::path& save,const Checkpoint& checkpoint) {
    if(checkpoint) checkpoint();
    std::vector<SaveFile> files;
    if(!fs::exists(save)) return files;
    require(!linked(save) && fs::is_directory(save),"Session-save root must be a real directory");
    size_t visited=0;uint64_t total=12;
    for(fs::recursive_directory_iterator it(save),end;it!=end;++it) {
        if(checkpoint) checkpoint();
        require(++visited<=16384 && it.depth()<=64,"Session-save tree is too large or deeply nested");
        const auto& entry=*it;
        require(!linked(entry.path()),"Session-save files cannot contain links");
        if(!entry.is_regular_file()) {require(entry.is_directory(),"Unsupported session-save file type");continue;}
        require(files.size()<max_files,"Too many session-save files");
        const auto relative=entry.path().lexically_relative(save);
        const auto name=relative.generic_string();normalized_name(name);
        const auto size=entry.file_size();
        require(size<=kMaxSaveBytes && total+12+name.size()<=kMaxSaveBytes-size,"Session save exceeds 64 MiB");
        total+=12+name.size()+size;
        files.push_back({relative,size,entry.last_write_time()});
    }
    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return a.relative<b.relative;});
    return files;
}
void unchanged(const fs::path& save,const SaveFile& file) {
    const auto path=host_path(save/file.relative);
    require(!linked(path) && fs::file_size(path)==file.size && fs::last_write_time(path)==file.modified,
            "Session save changed during preparation; close the game and retry");
}

}
Fingerprint digest(std::span<const uint8_t> bytes) {
    UcSha256 hash; uc_sha256_init(&hash); uc_sha256_update(&hash,bytes.data(),bytes.size());
    Fingerprint result{}; uc_sha256_final(&hash,result.data()); return result;
}
std::vector<uint8_t> capture_save(const fs::path& save_path,const Checkpoint& checkpoint) {
    const auto save=host_path(save_path);
    const auto files=scan(save,checkpoint);
    std::vector<uint8_t> out{'S','W','L','S','A','V','E','1',0,0,0,0};
    for(size_t i=0;i<4;++i) out[8+i]=uint8_t(files.size()>>(8*i));
    for(const auto& file:files) {
        if(checkpoint) checkpoint();
        unchanged(save,file);
        const auto name=file.relative.generic_string();
        put(out,name.size(),2);put(out,0,2);put(out,file.size,8);out.insert(out.end(),name.begin(),name.end());
        const auto from=out.size();out.resize(from+size_t(file.size));
        std::ifstream in(host_path(save/file.relative),std::ios::binary);
        require(bool(in),"Could not open the session save");
        size_t at=0;
        while(at<file.size) {
            if(checkpoint) checkpoint();
            const auto count=std::min<size_t>(65536,size_t(file.size)-at);
            in.read(reinterpret_cast<char*>(out.data()+from+at),count);
            require(bool(in),"Could not finish reading the session save");at+=count;
        }
        unchanged(save,file);
    }
    // Also reject additions/removals and changes to an earlier file while later
    // files were copied. Empty directories carry no save data in this archive.
    require(files==scan(save,checkpoint),"Session save changed during preparation; close the game and retry");
    parse(out);return out;
}
void restore_save(std::span<const uint8_t> bytes,const fs::path& destination_path,const Checkpoint& checkpoint) {
    if(checkpoint) checkpoint();
    const auto records=parse(bytes);
    const auto destination=host_path(destination_path);
    require(!fs::exists(destination),"Session-save destination already exists");
    require(fs::is_directory(destination.parent_path()) && !linked(destination.parent_path()),"Session-save parent must be a real directory");
    require(fs::create_directory(destination),"Could not create the session-save destination");
    try {
        for(const auto& record:records) {
            if(checkpoint) checkpoint();
            const auto path=host_path(destination/fs::path(record.name));
            fs::create_directories(path.parent_path());
            std::ofstream out(path,std::ios::binary);
            require(bool(out),"Opening the session save for writing failed");
            size_t at=0;
            while(at<record.bytes.size()) {
                if(checkpoint) checkpoint();
                const auto count=std::min<size_t>(65536,record.bytes.size()-at);
                out.write(reinterpret_cast<const char*>(record.bytes.data()+at),count);
                require(bool(out),"Writing the session save failed");at+=count;
            }
            out.flush();require(bool(out),"Flushing the session save failed");
        }
        if(checkpoint) checkpoint();
    } catch(...) {
        // Only this call's newly created destination is eligible for rollback.
        // Never overwrite or remove an existing profile or follow a replaced root.
        std::error_code ec;
        if(!linked(destination)) fs::remove_all(destination,ec);
        if(ec) throw std::runtime_error("Session-save restore failed and the partial copy could not be removed");
        throw;
    }
}
}
