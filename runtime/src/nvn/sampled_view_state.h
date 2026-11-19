#pragma once
#include <algorithm>
#include <array>
#include <optional>
#include <vulkan/vulkan.h>
namespace nx::nvn {
inline VkComponentSwizzle sampled_swizzle(uint32_t value,VkComponentSwizzle fallback) {
    constexpr VkComponentSwizzle map[]={VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ONE,VK_COMPONENT_SWIZZLE_R,
        VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A};
    return value<6 ? map[value] : fallback;
}
inline std::optional<std::array<uint32_t,4>> sampled_range(uint32_t image_levels,uint32_t image_layers,
    uint32_t base,uint32_t levels,uint32_t first_layer,uint32_t layers,VkImageViewType type) {
    if(!image_levels || !image_layers) return {};
    base=std::min(base,image_levels-1);first_layer=std::min(first_layer,image_layers-1);
    levels=levels ? std::min(levels,image_levels-base) : image_levels-base;
    layers=layers ? std::min(layers,image_layers-first_layer) : image_layers-first_layer;
    if(type==VK_IMAGE_VIEW_TYPE_1D || type==VK_IMAGE_VIEW_TYPE_2D || type==VK_IMAGE_VIEW_TYPE_3D) layers=1;
    if(type==VK_IMAGE_VIEW_TYPE_CUBE) { if(layers<6) return {};layers=6; }
    if(type==VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) { layers-=layers%6;if(!layers) return {}; }
    return std::array<uint32_t,4>{base,levels,first_layer,layers};
}
}
