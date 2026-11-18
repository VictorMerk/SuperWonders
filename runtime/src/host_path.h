#pragma once
#include <algorithm>
#include <filesystem>

namespace nx {
// Win32's ordinary paths can silently report an existing game resource as missing at MAX_PATH.
// Extended absolute paths work independently of the executable manifest and system long-path policy.
inline std::filesystem::path host_path(const std::filesystem::path& path) {
#if defined(_WIN32)
    if (path.empty()) return path;
    auto value = path.native();
    std::replace(value.begin(), value.end(), L'/', L'\\');
    if (value.starts_with(L"\\\\?\\") || value.starts_with(L"\\\\.\\")) return value;
    value = std::filesystem::absolute(std::filesystem::path(value)).lexically_normal().native();
    std::replace(value.begin(), value.end(), L'/', L'\\');
    if (value.starts_with(L"\\\\")) return L"\\\\?\\UNC\\" + value.substr(2);
    return L"\\\\?\\" + value;
#else
    return path;
#endif
}
}
