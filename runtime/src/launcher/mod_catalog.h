#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nx::launcher {
struct ModInfo {
    std::string name;
    bool on = false, active = false;
    std::filesystem::path folder;
    std::filesystem::path romfs;
    uint64_t files = 0, bytes = 0, shadowed = 0;
    std::string issue;
    std::vector<std::string> conflicts;  // bounded examples, first enabled mod wins
};
std::vector<ModInfo> inspect_mods(const std::filesystem::path& root, std::string_view aspect = {});
}
