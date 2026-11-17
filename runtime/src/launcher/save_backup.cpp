#include "save_backup.h"
#include <chrono>
#include <fstream>

namespace nx::launcher {
std::filesystem::path backup_saves(const std::filesystem::path& data, std::string& error) {
    namespace fs = std::filesystem;
    error.clear();
    const auto source = data / "user", backups = data / "backups";
    std::error_code ec;
    if (!fs::is_directory(source, ec)) { error = "No save folder exists yet."; return {}; }
    if (fs::is_symlink(source, ec)) { error = "The save folder is a link; back up its target separately."; return {}; }
    fs::create_directories(backups, ec);
    if (ec) { error = "Could not create the backups folder: " + ec.message(); return {}; }
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    fs::path temporary, destination;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        const auto name = "saves-" + std::to_string(stamp) + "-" + std::to_string(attempt);
        destination = backups / name; temporary = backups / (name + ".partial");
        if (fs::exists(destination, ec)) continue;
        if (fs::create_directory(temporary, ec)) break;
        temporary.clear();
        if (ec && ec != std::errc::file_exists) break;
    }
    if (temporary.empty()) { error = "Could not create a new backup: " + ec.message(); return {}; }
    auto fail = [&]() -> fs::path {
        error = "Backup incomplete; saves are unchanged. Details: " + ec.message();
        return {};
    };
    fs::create_directory(temporary / "user", ec);
    if (ec) return fail();
    fs::recursive_directory_iterator it(source, ec), end;
    if (ec) return fail();
    while (it != end) {
        const auto entry = *it;
        if (entry.is_symlink(ec)) { ec = std::make_error_code(std::errc::too_many_symbolic_link_levels); return fail(); }
        if (ec) return fail();
        const auto relative = entry.path().lexically_relative(source);
        const auto target = temporary / "user" / relative;
        if (entry.is_directory(ec)) fs::create_directory(target, ec);
        else if (!ec && entry.is_regular_file(ec)) fs::copy_file(entry.path(), target, fs::copy_options::none, ec);
        if (ec) return fail();
        it.increment(ec);
        if (ec) return fail();
    }
    fs::rename(temporary, destination, ec);
    if (ec) return fail();
    return destination;
}
}
