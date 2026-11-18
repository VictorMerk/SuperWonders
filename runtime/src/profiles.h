#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace nx::profiles {
struct Profile { std::string id, name; };
std::vector<Profile> list(const std::filesystem::path& data);
Profile active(const std::filesystem::path& data);
bool select(const std::filesystem::path& data, const std::string& id, std::string& error);
bool create(const std::filesystem::path& data, const std::string& name, std::string& error);
bool rename(const std::filesystem::path& data, const std::string& name, std::string& error);
// Default keeps the original user/ tree and guest account UID. Additional profiles isolate storage only.
std::filesystem::path storage_root(const std::filesystem::path& data);
std::filesystem::path user_root(const std::filesystem::path& data);
std::string nickname(const std::filesystem::path& data);
}
