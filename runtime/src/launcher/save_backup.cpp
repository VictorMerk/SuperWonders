#include "save_backup.h"
#include <chrono>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace nx::launcher {
namespace {
namespace fs = std::filesystem;
bool linked(const fs::path& p, std::error_code& ec) {
#if defined(_WIN32)
    const auto attributes=GetFileAttributesW(p.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES) {ec=std::error_code(GetLastError(),std::system_category());return true;}
    return (attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
#else
    return fs::is_symlink(fs::symlink_status(p,ec));
#endif
}
void copy_tree(const fs::path& from,const fs::path& to) {
    std::error_code ec;
    if(linked(from,ec) || ec || !fs::is_directory(from)) throw std::runtime_error("Save folder must be a regular directory.");
    fs::create_directory(to);
    uint64_t bytes=0; size_t count=0;
    for(const auto& entry:fs::recursive_directory_iterator(from)) {
        if(linked(entry.path(),ec) || ec) throw std::runtime_error("Save folders must not contain links or junctions.");
        if(++count>16384) throw std::runtime_error("Save folder contains too many entries.");
        const auto target=to/entry.path().lexically_relative(from);
        if(entry.is_directory()) fs::create_directory(target);
        else if(entry.is_regular_file()) {
            bytes+=entry.file_size(); if(bytes>(1ull<<30)) throw std::runtime_error("Save folder exceeds 1 GiB.");
            const auto size=entry.file_size(); const auto time=entry.last_write_time();
            fs::copy_file(entry.path(),target);
            if(size!=fs::file_size(target) || size!=entry.file_size() || time!=entry.last_write_time())
                throw std::runtime_error("A save changed during copying. Try again after closing the game.");
        } else throw std::runtime_error("Unsupported save file type.");
    }
}
}

std::filesystem::path backup_saves(const std::filesystem::path& data, std::string& error) {
    namespace fs = std::filesystem;
    error.clear();
    const auto source = data / "user", backups = data / "backups";
    std::error_code ec;
    if (!fs::is_directory(source, ec)) { error = "No save folder exists yet."; return {}; }
    if (linked(source, ec)) { error = "The save folder is a link; back up its target separately."; return {}; }
    fs::create_directories(backups, ec);
    if (ec) { error = "Could not create the backups folder: " + ec.message(); return {}; }
    if(linked(backups,ec) || ec) {error="The backups folder must not be a link or junction.";return {};}
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
    try {
        copy_tree(source,temporary/"user");
        fs::rename(temporary,destination);
    } catch(const std::exception& e) {
        error=std::string("Backup incomplete; active saves are unchanged. ")+e.what();return {};
    }
    return destination;
}
}

namespace nx::launcher {
std::vector<std::filesystem::path> list_save_backups(const std::filesystem::path& data) {
    namespace fs=std::filesystem;
    std::vector<fs::path> result; std::error_code ec;
    for(fs::directory_iterator it(data/"backups",ec),end; !ec && it!=end;it.increment(ec)) {
        if(it->path().extension()==".partial" || linked(it->path(),ec) || ec) continue;
        if(fs::is_directory(it->path()/"user/save",ec)) result.push_back(it->path());
    }
    std::sort(result.rbegin(),result.rend()); return result;
}
bool restore_saves(const std::filesystem::path& data,const std::filesystem::path& backup,std::string& error) {
    namespace fs=std::filesystem; error.clear();
    fs::path stage,safety;
    try {
        std::error_code ec;
        if(linked(data/"backups",ec) || ec)throw std::runtime_error("The backups folder must not be a link or junction.");
        if(backup.parent_path().lexically_normal()!=(data/"backups").lexically_normal() ||
            backup.filename().empty() || backup.filename()==".." || backup.filename()=="." ||
            backup.extension()==".partial" || linked(backup,ec) || ec || !fs::is_directory(backup/"user/save"))
            throw std::runtime_error("Choose a completed backup from this profile's backups folder.");
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        stage=data/"backups"/("restore-"+std::to_string(stamp)+".partial");
        if(!fs::create_directory(stage)) throw std::runtime_error("Could not prepare restore folder.");
        copy_tree(backup/"user",stage/"user");
        const bool have_user=fs::exists(data/"user");
        if(have_user) {
            safety=backup_saves(data,error); if(safety.empty()) return false;
            fs::rename(data/"user",safety/"original-user");
        }
        try { fs::rename(stage/"user",data/"user"); }
        catch(...) {
            if(have_user) {
                std::error_code rollback; fs::rename(safety/"original-user",data/"user",rollback);
                if(rollback) {error="Restore failed. Previous saves are preserved in "+safety.string()+"; reopen the launcher before playing.";return false;}
            }
            throw;
        }
        fs::remove(stage,ec); return true;
    } catch(const std::exception& e) { error=std::string("Restore failed; backup preserved. ")+e.what(); return false; }
}
}
