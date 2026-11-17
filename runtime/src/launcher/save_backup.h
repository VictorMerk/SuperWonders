#pragma once
#include <filesystem>
#include <string>
namespace nx::launcher {
// A new backup directory each time. Existing backups and active saves are never overwritten.
std::filesystem::path backup_saves(const std::filesystem::path& data, std::string& error);
}
