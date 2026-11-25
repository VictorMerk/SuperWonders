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
    std::string issue, compatibility;
    std::vector<std::string> conflicts;  // bounded examples, first enabled mod wins
};
enum class ModFilter { All, Active, Issues, Conflicts, Inactive };
bool mod_matches(const ModInfo& mod,std::string_view query,ModFilter filter);
bool write_mod_report(const std::vector<ModInfo>& mods,const std::filesystem::path& destination,std::string& error);
std::vector<ModInfo> inspect_mods(const std::filesystem::path& root,std::string_view aspect = {},
                                std::string_view input_id = {},std::string* warning = nullptr);
}
