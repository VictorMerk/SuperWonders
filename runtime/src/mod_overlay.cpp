#include "mod_overlay.h"
#include "host_path.h"
#include <algorithm>
#include <stdexcept>
#include <fstream>
#include <unordered_set>
#include <chrono>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace nx::mods {
namespace fs = std::filesystem;
namespace {
std::string text(const fs::path& p) { const auto s=p.generic_u8string(); return {s.begin(),s.end()}; }
std::string lower(std::string s) {
    for(auto& c:s) if(c>='A' && c<='Z') c+='a'-'A';
    return s;
}
std::string key(const fs::path& p) {
    auto s=text(p.lexically_normal());
#if defined(_WIN32)
    s=lower(std::move(s));
#endif
    return s;
}
bool title_matches(const fs::path& relative) {
    // Multi-game Atmosphere bundles must not select another title's first romfs.
    std::string previous;
    for(const auto& part:relative) {
        const auto current=lower(text(part));
        if(previous=="contents" && current!="010015100b514000") return false;
        previous=current;
    }
    return true;
}
}
std::string logical_name(std::string_view name){if(name.starts_with('-'))name.remove_prefix(1);return std::string(name);}
namespace {
void valid_order_name(const std::string& name){
    if(name.empty()||name=="."||name==".."||name.size()>1024||name.find_first_of("/\\:\r\n")!=name.npos||name.find('\0')!=name.npos)
        throw std::runtime_error("Invalid custom mod-order entry");
}
std::vector<std::string> read_order(const fs::path& folder,const std::function<void()>& checkpoint){
    const auto path=host_path(folder/"load-order.txt");if(!fs::exists(path))return {};
    if(fs::file_size(path)>65536)throw std::runtime_error("Custom mod order exceeds 64 KiB");
    std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Could not read custom mod order");
    std::vector<std::string> names;std::unordered_set<std::string> unique;std::string name;
    while(std::getline(in,name)){
        if(checkpoint)checkpoint();if(name.ends_with('\r'))name.pop_back();if(name.empty())continue;
        valid_order_name(name);if(!unique.insert(name).second||names.size()>=256)throw std::runtime_error("Duplicate or excessive custom mod-order entries");names.push_back(name);
    }
    if(!in.eof())throw std::runtime_error("Could not finish reading custom mod order");return names;
}
}
void apply_order(std::vector<Root>& roots,const fs::path& folder,const std::function<void()>& checkpoint){
    const auto names=read_order(folder,checkpoint);std::unordered_map<std::string,size_t> rank;
    for(size_t i=0;i<names.size();++i)rank.emplace(names[i],i);
    const auto position=[&](const Root& root){const auto it=rank.find(logical_name(root.name));return it==rank.end()?names.size():it->second;};
    std::stable_sort(roots.begin(),roots.end(),[&](const auto&a,const auto&b){return position(a)<position(b);});
}
void save_order(const fs::path& folder,const std::vector<std::string>& names){
    if(names.size()>256)throw std::runtime_error("Custom mod order supports up to 256 entries");
    std::unordered_set<std::string> unique;size_t bytes=0;
    for(const auto&name:names){valid_order_name(name);if(!unique.insert(name).second)throw std::runtime_error("Duplicate mod names prevent changing load order");bytes+=name.size()+1;}
    if(bytes>65536)throw std::runtime_error("Custom mod order exceeds 64 KiB");
    fs::create_directories(host_path(folder));const auto target=host_path(folder/"load-order.txt");
    auto temporary=target;temporary+=".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try{
        {std::ofstream out(temporary,std::ios::binary);if(!out)throw std::runtime_error("Could not create custom mod order");for(const auto&name:names)out<<name<<'\n';out.flush();if(!out)throw std::runtime_error("Could not write custom mod order");}
#if defined(_WIN32)
        if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Could not replace custom mod order");
#else
        fs::rename(temporary,target);
#endif
    }catch(...){std::error_code ec;fs::remove(temporary,ec);throw;}
}
bool set_all_enabled(const fs::path& folder,bool on,std::string& error,const std::function<void()>& checkpoint){
    error.clear();std::vector<std::pair<fs::path,fs::path>> moves;size_t done=0;
    try{
        size_t count=0;for(const auto&entry:fs::directory_iterator(host_path(folder))){
            if(++count>4096)throw std::runtime_error("Too many entries in mods folder");if(!entry.is_directory())continue;
            const auto name=text(entry.path().filename()),logical=logical_name(name);
            if(logical.starts_with("00-uw-"))continue; // aspect-controlled built-ins retain their selection semantics
            if((!name.starts_with('-'))==on)continue;
            const auto target=host_path(folder/fs::u8path(on?logical:"-"+logical));
            if(fs::exists(target))throw std::runtime_error("A duplicate enabled/disabled mod blocks this change; no mods changed");
            moves.push_back({entry.path(),target});
        }
        std::sort(moves.begin(),moves.end());
        for(const auto&move:moves){if(checkpoint)checkpoint();fs::rename(move.first,move.second);++done;}return true;
    }catch(const std::exception&e){
        bool rolled_back=true;while(done){--done;std::error_code ec;fs::rename(moves[done].second,moves[done].first,ec);rolled_back&=!ec;}
        error=rolled_back?e.what():"Could not finish rolling back mod names; inspect the mods folder before playing";return false;
    }
}
bool linked(const fs::directory_entry& entry) {
    std::error_code ec;
    if(entry.is_symlink(ec)) return true;
#if defined(_WIN32)
    const auto attrs=GetFileAttributesW(host_path(entry.path()).c_str());
    if(attrs!=INVALID_FILE_ATTRIBUTES && (attrs&FILE_ATTRIBUTE_REPARSE_POINT)) return true;
#endif
    return false;
}
bool enabled(std::string_view name,std::string_view aspect) {
    const bool off=name.starts_with('-');
    if(off) name.remove_prefix(1);
    const bool wide32=name=="00-uw-map-hud-32-9" || name=="00-uw-screen-test";
    const bool wide21=name=="00-uw-map-hud-21-9" || name=="00-uw-screen-test-21-9";
    if(!aspect.empty() && (wide32||wide21))
        return (wide32&&aspect=="32:9") || (wide21&&aspect=="21:9");
    return !off;
}
fs::path find_romfs(const fs::path& folder,const std::function<void()>& checkpoint) {
    if(checkpoint) checkpoint();
    std::error_code ec;
    const auto direct=host_path(folder/"romfs");
    if(fs::is_directory(direct,ec)) return direct;
    ec.clear();
    std::vector<fs::path> candidates;
    size_t visited=0;
    for(fs::recursive_directory_iterator it(host_path(folder),ec),end; !ec&&it!=end;it.increment(ec)) {
        if(checkpoint) checkpoint();
        if(++visited>100000) throw std::runtime_error("Mod folder inspection limit exceeded");
        if(it.depth()>=3 || linked(*it)) it.disable_recursion_pending();
        if(it->is_directory(ec) && lower(text(it->path().filename()))=="romfs" &&
           title_matches(it->path().lexically_relative(host_path(folder)))) {
            candidates.push_back(it->path());
            it.disable_recursion_pending();
        }
    }
    if(ec) throw fs::filesystem_error("Could not inspect mod folder",folder,ec);
    std::sort(candidates.begin(),candidates.end());
    return candidates.empty()?fs::path{}:candidates.front();
}
std::vector<Root> selected(const fs::path& folder,std::string_view aspect,const std::function<void()>& checkpoint) {
    if(checkpoint) checkpoint();
    std::error_code ec;
    if(!fs::exists(host_path(folder),ec)) {
        if(ec) throw fs::filesystem_error("Could not inspect mods",folder,ec);
        return {};
    }
    std::vector<Root> result;
    size_t inspected=0;
    for(const auto& entry:fs::directory_iterator(host_path(folder))) {
        if(checkpoint) checkpoint();
        if(++inspected>4096) throw std::runtime_error("Too many entries in the mods folder (limit 4096)");
        const auto name=text(entry.path().filename());
        if(entry.is_directory() && enabled(name,aspect)) result.push_back({entry.path(),{},name});
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.folder<b.folder;});
    apply_order(result,folder,checkpoint);
    for(auto& root:result) root.romfs=find_romfs(root.folder,checkpoint);
    return result;
}
Overlay::Overlay(const std::vector<Root>& roots,size_t entry_limit) {
    size_t visited=0;
    for(const auto& root:roots) {
        if(root.romfs.empty()) continue;
        Index index{host_path(root.romfs),false};
        std::error_code ec;
        for(fs::recursive_directory_iterator it(index.root,ec),end; !ec&&it!=end;it.increment(ec)) {
            if(visited++>=entry_limit) { index.fallback=true; break; }
            if(it.depth()>=64) { index.fallback=true; it.disable_recursion_pending(); continue; }
            if(linked(*it)) { index.fallback=true; it.disable_recursion_pending(); continue; }
            if(!it->is_regular_file(ec)) continue;
            const auto relative=it->path().lexically_relative(index.root);
#if defined(_WIN32)
            // Preserve Win32 Unicode case matching through the old filesystem path.
            const auto name=text(relative);
            if(std::any_of(name.begin(),name.end(),[](unsigned char c){return c>=128;})) {
                index.fallback=true; continue;
            }
#endif
            paths_[key(relative)].push_back({roots_.size(),it->path()}); ++files_;
        }
        if(ec) index.fallback=true;
        if(index.fallback) fallback_.push_back(roots_.size());
        roots_.push_back(std::move(index));
    }
}
std::optional<fs::path> Overlay::resolve(std::string_view relative) const {
    // Only guest-relative RomFS names belong in an overlay, including fallback roots.
    if(relative.empty() || relative.find('\0')!=relative.npos || relative.find(':')!=relative.npos) return {};
    const auto path=fs::u8path(relative);
    if(path.is_absolute() || path.has_root_path()) return {};
    for(const auto& part:path) if(part=="..") return {};
    const auto found=paths_.find(key(path));
    const std::vector<Candidate> empty;
    const auto& candidates=found==paths_.end()?empty:found->second;
    size_t next=0;
    auto exists=[](const fs::path& p) { std::error_code ec; return fs::is_regular_file(p,ec); };
    // Merge indexed candidates with only the roots needing filesystem fallback.
    // A normal miss takes one hash lookup regardless of the number of enabled mods.
    for(const auto fallback:fallback_) {
        while(next<candidates.size() && candidates[next].root<fallback) {
            const auto& candidate=candidates[next++];
            if(exists(candidate.path)) return candidate.path;
        }
        const auto candidate=host_path(roots_[fallback].root/path);
        if(exists(candidate)) return candidate;
        while(next<candidates.size() && candidates[next].root==fallback) ++next;
    }
    while(next<candidates.size()) {
        const auto& candidate=candidates[next++];
        if(exists(candidate.path)) return candidate.path;
    }
    return {};
}
size_t Overlay::fallback_roots() const {
    return std::count_if(roots_.begin(),roots_.end(),[](const auto& root){return root.fallback;});
}
}
