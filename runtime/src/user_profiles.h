#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nx::profiles {
struct User {
    uint64_t lo = 1, hi = 0x4E58485553455231ull;
    std::string name = "Zagreus";
    std::string icon;
};
struct Database {
    std::vector<User> users{User{}};
    size_t selected = 0;
    bool valid = true;
};
const User& current();
Database load(const std::filesystem::path& root);
bool save(const std::filesystem::path& root, const Database& db);
bool valid_icon(const std::vector<uint8_t>& image);
User make_user(const Database& db, const std::string& name);
std::filesystem::path save_path(const std::filesystem::path& root, const User& user);
// Copies through a staging directory and preserves any old destination as a sibling backup.
bool copy_save(const std::filesystem::path& root, const User& source, const User& destination,
               std::filesystem::path& backup, std::string& error);
// A manual snapshot shares the same directory format as backups created by copy/import.
bool snapshot_save(const std::filesystem::path& root, const User& user,
                   std::filesystem::path& snapshot, std::string& error);
std::vector<std::filesystem::path> save_backups(const std::filesystem::path& root, const User& user);
bool restore_save(const std::filesystem::path& root, const User& user, const std::filesystem::path& snapshot,
                  std::filesystem::path& previous, std::string& error);
struct EmulatorSave {
    std::string label;
    std::filesystem::path path;
};
std::vector<EmulatorSave> detect_emulator_saves();
bool import_save(const std::filesystem::path& root, const std::filesystem::path& source_dir,
                 const User& destination, std::filesystem::path& backup, std::string& error);
}
