#pragma once
#include <array>
#include <functional>
#include <filesystem>
#include <string_view>
namespace nx::lan {
std::array<unsigned char,32> content_fingerprint(const std::filesystem::path& data,
    const std::filesystem::path& runtime,std::string_view revision,std::string_view aspect,
    const std::function<void()>& checkpoint = {});
}
