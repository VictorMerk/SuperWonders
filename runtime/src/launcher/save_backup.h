#pragma once
#include <filesystem>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace nx::launcher {
using SaveCheckpoint=std::function<void()>;
struct BackupSummary { uint64_t files=0,bytes=0; bool has_integrity=false; };
std::filesystem::path backup_saves(const std::filesystem::path& data,std::string& error,const SaveCheckpoint& checkpoint={});
std::vector<std::filesystem::path> list_save_backups(const std::filesystem::path& data);
bool restore_saves(const std::filesystem::path& data,const std::filesystem::path& backup,std::string& error,const SaveCheckpoint& checkpoint={});
bool verify_save_backup(const std::filesystem::path& backup,std::string& error,const SaveCheckpoint& checkpoint={});
BackupSummary backup_summary(const std::filesystem::path& backup,std::string& error);
}
