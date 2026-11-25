#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nx::mods {
struct Root {
    std::filesystem::path folder, romfs;
    std::string name;
};
bool enabled(std::string_view name, std::string_view aspect = {});
std::filesystem::path find_romfs(const std::filesystem::path& folder);
std::vector<Root> selected(const std::filesystem::path& folder, std::string_view aspect = {});
bool linked(const std::filesystem::directory_entry& entry);

// Snapshot the file names at boot. Contents remain on disk; removals still fall through
// to the next mod. New files and load-order changes take effect on the next launch.
class Overlay {
public:
    explicit Overlay(const std::vector<Root>& roots, size_t entry_limit = 100000);
    std::optional<std::filesystem::path> resolve(std::string_view relative) const;
    size_t files() const { return files_; }
    size_t fallback_roots() const;
private:
    struct Index {
        std::filesystem::path root;
        std::unordered_map<std::string, std::filesystem::path> paths;
        bool fallback = false;
    };
    std::vector<Index> roots_;
    size_t files_ = 0;
};
}
