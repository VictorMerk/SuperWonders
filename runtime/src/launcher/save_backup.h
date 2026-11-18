#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace nx::launcher {
// A new backup directory each time. Existing backups and active saves are never overwritten.
std::filesystem::path backup_saves(const std::filesystem::path& data, std::string& error);
std::vector<std::filesystem::path> list_save_backups(const std::filesystem::path& data);
// Launcher only: validate/copy first, preserve current saves, then swap with rollback on failure.
bool restore_saves(const std::filesystem::path& data, const std::filesystem::path& backup, std::string& error);
}
