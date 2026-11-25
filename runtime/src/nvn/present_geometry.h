#pragma once
#include <algorithm>
#include <cstdint>

namespace nx::nvn {
struct PictureRect { uint32_t x = 0, y = 0, width = 0, height = 0; };
inline bool valid_picture_crop(const int32_t* crop, uint32_t width, uint32_t height) {
    return crop && crop[0] >= 0 && crop[1] >= 0 && crop[2] > 0 && crop[3] > 0 &&
        uint64_t(crop[0]) + uint32_t(crop[2]) <= width &&
        uint64_t(crop[1]) + uint32_t(crop[3]) <= height;
}
inline PictureRect fit_picture(uint32_t width, uint32_t height, uint32_t output_width, uint32_t output_height, bool preserve) {
    PictureRect rect{0, 0, output_width, output_height};
    if (!preserve || !width || !height || !output_width || !output_height) return rect;
    if (uint64_t(output_width) * height <= uint64_t(output_height) * width)
        rect.height = std::max(1u, uint32_t(uint64_t(output_width) * height / width));
    else rect.width = std::max(1u, uint32_t(uint64_t(output_height) * width / height));
    rect.x = (output_width - rect.width) / 2;
    rect.y = (output_height - rect.height) / 2;
    return rect;
}
}
