#include "launcher/game_source.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <deque>

namespace nx::launcher::source {
namespace fs = std::filesystem;
namespace {
bool regular(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}
void add(std::vector<std::string>& paths, const fs::path& path) {
    std::error_code ec;
    auto full = fs::weakly_canonical(path, ec);
    if (ec) full = path.lexically_normal();
    const auto u8 = full.u8string();
    std::string value(reinterpret_cast<const char*>(u8.data()), u8.size());
    if (std::find(paths.begin(), paths.end(), value) != paths.end()) return;
    for (const auto& existing : paths) {
        const fs::path other(reinterpret_cast<const char8_t*>(existing.c_str()));
        if (fs::equivalent(full, other, ec) && !ec) return;
        ec.clear();
    }
    paths.push_back(value);
}
}
bool exefs_ok(const fs::path& folder) {
    return regular(folder / "main") && regular(folder / "sdk");
}
bool romfs_ok(const fs::path& folder) {
    std::error_code ec;
    // Match setup_game.py's Shader/*.zs requirement, not just the directory name.
    for (fs::directory_iterator it(folder / "Shader", fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        auto ext = it->path().extension().string();
#if defined(_WIN32)
        for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
#endif
        if (ext == ".zs" && regular(it->path())) return true;
    }
    return false;
}
Found discover(const fs::path& folder) {
    Found out;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return out;
    const bool direct_exefs = exefs_ok(folder), direct_romfs = romfs_ok(folder);
    // Selecting ExeFS/RomFS itself also finds a sibling extraction. Do not search
    // arbitrary parents when a broad, unrelated folder was selected.
    const bool container = romfs_ok(folder / "romfs") || exefs_ok(folder / "exefs");
    const auto root = (direct_exefs || direct_romfs) && !container && folder.has_parent_path() ? folder.parent_path() : folder;
    if (direct_exefs) add(out.exefs, folder);
    if (direct_romfs) add(out.romfs, folder);
    std::deque<std::pair<fs::path, unsigned>> pending{{root, 0}};
    size_t entries = 0, folders = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!pending.empty()) {
        if (++folders > 512 || std::chrono::steady_clock::now() >= deadline) { out.limited = true; break; }
        auto [path, depth] = pending.front(); pending.pop_front();
        const bool exe = exefs_ok(path), rom = romfs_ok(path);
        if (exe) add(out.exefs, path);
        if (rom) add(out.romfs, path);
        // No need to walk thousands of assets inside an already identified dump.
        if (rom) continue;
        for (fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (++entries > 10000 || std::chrono::steady_clock::now() >= deadline) { out.limited = true; break; }
            std::error_code e;
            if (it->is_symlink(e) || !it->is_directory(e)) continue;
            const auto name = it->path().filename().u8string();
            if (name.empty() || name[0] == '.' || name[0] == '$') continue;
            if (name == u8"game-build") continue;  // launcher compiler kit, not the player's extraction
            if (depth >= 4) { out.limited = true; continue; }
            if (pending.size() >= 512) { out.limited = true; continue; }
            pending.emplace_back(it->path(), depth + 1);
        }
        ec.clear();
        if (entries > 10000) break;
    }
    std::sort(out.exefs.begin(), out.exefs.end());
    std::sort(out.romfs.begin(), out.romfs.end());
    return out;
}
}
