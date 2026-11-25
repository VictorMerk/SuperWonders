#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace nx::launcher::source {
// Read-only, bounded discovery. A selected directory may contain the two extracted
// folders, or be either one of them. Multiple candidates are never silently chosen.
struct Found {
    std::vector<std::string> exefs, romfs;
    bool limited = false;
};
bool exefs_ok(const std::filesystem::path& folder);
bool romfs_ok(const std::filesystem::path& folder);
Found discover(const std::filesystem::path& folder);
}
