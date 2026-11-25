#include "mod_catalog.h"
#include "mod_overlay.h"
#include "host_path.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace nx::launcher {
namespace fs = std::filesystem;
namespace {
std::string text(const fs::path& path) {
    auto s = path.generic_u8string();
    return std::string(s.begin(),s.end());
}
std::string key(std::string s) {
#if defined(_WIN32)
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a'-'A';
#endif
    return s;
}

}
std::vector<ModInfo> inspect_mods(const fs::path& root, std::string_view aspect,std::string_view input_id,std::string* warning) {
    std::vector<ModInfo> mods;
    if(warning)warning->clear();
    std::error_code ec;
    for (fs::directory_iterator it(host_path(root),ec), end; !ec && it != end; it.increment(ec)) {
        if (mods.size() >= 4096){if(warning)*warning="Too many mod folders; listing is incomplete.";break;}
        if (!it->is_directory(ec)) continue;
        const auto name = text(it->path().filename());
        ModInfo m;
        m.on = !name.starts_with('-');
        m.active = nx::mods::enabled(name,aspect);
        m.folder = it->path();
        m.name = m.on ? name : name.substr(1);
        if (nx::mods::linked(*it)) m.issue = "Linked mod folder: not inspected.";
        mods.push_back(std::move(m));
    }
    std::sort(mods.begin(),mods.end(),[](const auto& a,const auto& b) { return a.folder < b.folder; });
    if(ec&&ec!=std::errc::no_such_file_or_directory&&warning)*warning="Could not inspect all mod folders: "+ec.message();
    std::vector<nx::mods::Root> ordered;
    for(const auto&mod:mods)ordered.push_back({mod.folder,{},text(mod.folder.filename())});
    try{nx::mods::apply_order(ordered,root);}
    catch(const std::exception&error){if(warning)*warning=std::string("Custom order needs repair/reset: ")+error.what();}
    std::map<fs::path,size_t> rank;for(size_t i=0;i<ordered.size();++i)rank[ordered[i].folder]=i;
    std::stable_sort(mods.begin(),mods.end(),[&](const auto&a,const auto&b){return rank[a.folder]<rank[b.folder];});
    if(mods.size()>256){mods.resize(256);if(warning)*warning="Showing the first 256 mods; report is incomplete.";}
    std::map<std::string,std::string> owners;
    size_t visited = 0;
    for (auto& m : mods) {
        if (!m.issue.empty()) continue;
        const auto& dir = m.folder;
        const auto declaration=host_path(dir/"wonder-mod.ini");
        if(fs::is_regular_file(declaration,ec)){
            try{
                if(fs::file_size(declaration)>4096)throw std::runtime_error("Target declaration exceeds 4 KiB.");
                std::ifstream in(declaration);if(!in)throw std::runtime_error("Could not read target declaration.");
                std::string line;bool declared=false,match=false;
                while(std::getline(in,line))if(line.starts_with("input=")){
                    declared=true;std::istringstream values(line.substr(6));std::string target;
                    while(std::getline(values,target,',')){
                        const auto first=target.find_first_not_of(" \r\t"),last=target.find_last_not_of(" \r\t");
                        if(first==target.npos)throw std::runtime_error("Empty declared input target.");target=target.substr(first,last-first+1);
                        if((target.size()!=32&&target.size()!=64)||target.find_first_not_of("0123456789abcdefABCDEF")!=target.npos)throw std::runtime_error("Invalid declared input identity.");
                        auto identity=std::string(input_id);for(auto&c:target)if(c>='A'&&c<='F')c+='a'-'A';for(auto&c:identity)if(c>='A'&&c<='F')c+='a'-'A';
                        if(target==identity)match=true;
                    }
                }
                if(declared){m.compatibility= input_id.empty()?"Target declaration cannot be checked until game preparation completes.":match?"Declared input matches; gameplay compatibility is not verified.":"Declared input differs from this prepared game.";if(!match&&!input_id.empty())m.issue=m.compatibility;}
            }catch(const std::exception&error){m.issue=error.what();}
        }
        ec.clear();
        try { m.romfs=nx::mods::find_romfs(dir); }
        catch(const std::exception& error) { m.issue=error.what(); continue; }
        if (m.romfs.empty()) {
            if (m.issue.empty()) m.issue = "No romfs folder found: this mod will not load.";
            continue;
        }
        ec.clear();
        for (fs::recursive_directory_iterator it(m.romfs,ec), end; !ec && it != end; it.increment(ec)) {
            if(it.depth()>64){it.disable_recursion_pending();m.issue="Deep paths were not inspected; report may be incomplete.";if(warning)*warning="Mod inspection reached the depth limit.";continue;}
            if (++visited > 100000) { m.issue = "Inspection limit reached; report may be incomplete.";if(warning)*warning=m.issue; break; }
            if (nx::mods::linked(*it)) { it.disable_recursion_pending(); m.issue = "Linked entries were not inspected; counts and conflicts may be incomplete."; continue; }
            if (!it->is_regular_file(ec)) continue;
            ++m.files;
            const auto size = it->file_size(ec);
            if (ec) break;
            m.bytes += size;
            if (!m.active) continue;
            const auto relative = text(it->path().lexically_relative(m.romfs));
            auto [owner, inserted] = owners.emplace(key(relative),m.name);
            if (!inserted) {
                ++m.shadowed;
                if (m.conflicts.size() < 12) m.conflicts.push_back(relative+" -> "+owner->second);
            }
        }
        if (!m.files && m.issue.empty() && !ec) m.issue = "Empty romfs folder: this mod replaces no files.";
        if (ec) m.issue = "Could not read all files: "+ec.message();
    }
    return mods;
}
bool mod_matches(const ModInfo& mod,std::string_view query,ModFilter filter){
    std::string name=mod.name,needle(query);
    const auto fold=[](std::string& value){for(auto&c:value)if(c>='A'&&c<='Z')c+='a'-'A';};fold(name);fold(needle);
    if(name.find(needle)==name.npos)return false;
    switch(filter){case ModFilter::Active:return mod.active;case ModFilter::Issues:return !mod.issue.empty();case ModFilter::Conflicts:return mod.shadowed>0;case ModFilter::Inactive:return !mod.active;default:return true;}
}
bool write_mod_report(const std::vector<ModInfo>& mods,const fs::path& destination,std::string& error){
    error.clear();try{std::ofstream out(host_path(destination),std::ios::binary);if(!out)throw std::runtime_error("Could not create mod report.");
        out<<"Wonder mod report\nContains mod names and relative conflicts; no game files or absolute installation paths.\nReflects the inspected list only; check launcher warnings for incomplete inspection.\n";
        size_t order=0;for(const auto&m:mods){auto name=m.name;for(auto&c:name)if(static_cast<unsigned char>(c)<32)c='?';
            out<<++order<<". "<<name<<" | "<<(m.active?"active":"inactive")<<" | "<<m.files<<" files | "<<m.bytes<<" bytes | "<<m.shadowed<<" shadowed | issue="<<!m.issue.empty()<<'\n';
            if(!m.compatibility.empty())out<<m.compatibility<<'\n';
            for(const auto&conflict:m.conflicts)out<<"  "<<conflict<<'\n';}
        out.flush();if(!out)throw std::runtime_error("Could not finish mod report.");return true;
    }catch(const std::exception&e){error=e.what();return false;}
}

}
