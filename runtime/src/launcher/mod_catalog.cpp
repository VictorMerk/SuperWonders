#include "mod_catalog.h"
#include "mod_overlay.h"
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
std::vector<ModInfo> inspect_mods(const fs::path& root, std::string_view aspect) {
    std::vector<ModInfo> mods;
    std::error_code ec;
    for (fs::directory_iterator it(root,ec), end; !ec && it != end; it.increment(ec)) {
        if (mods.size() >= 256) break;
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
    std::map<std::string,std::string> owners;
    size_t visited = 0;
    for (auto& m : mods) {
        if (!m.issue.empty()) continue;
        const auto& dir = m.folder;
        try { m.romfs=nx::mods::find_romfs(dir); }
        catch(const std::exception& error) { m.issue=error.what(); continue; }
        if (m.romfs.empty()) {
            if (m.issue.empty()) m.issue = "No romfs folder found: this mod will not load.";
            continue;
        }
        ec.clear();
        for (fs::recursive_directory_iterator it(m.romfs,ec), end; !ec && it != end; it.increment(ec)) {
            if (++visited > 100000) { m.issue = "Inspection limit reached; report may be incomplete."; break; }
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
}
