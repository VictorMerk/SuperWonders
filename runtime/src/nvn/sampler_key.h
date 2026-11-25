#pragma once
#include <array>
#include <bit>
#include <vulkan/vulkan.h>
namespace nx::nvn {
inline VkSamplerAddressMode sampler_wrap(uint32_t mode, bool mirror_clamp) {
    switch(mode) {
        case 1: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case 2: case 3: case 4: return mirror_clamp ? VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case 5: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case 6: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        default: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
}
// The sampler path emits no pNext extensions. Encode fields explicitly rather
// than struct padding, and omit values Vulkan ignores for this configuration.
inline std::array<uint32_t,16> sampler_key(const VkSamplerCreateInfo& s) {
    const auto bits=[](float x) { return std::bit_cast<uint32_t>(x==0.0f ? 0.0f : x); };
    const bool border=s.addressModeU==VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
        s.addressModeV==VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER || s.addressModeW==VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    return {uint32_t(s.minFilter),uint32_t(s.magFilter),uint32_t(s.mipmapMode),uint32_t(s.addressModeU),
        uint32_t(s.addressModeV),uint32_t(s.addressModeW),bits(s.mipLodBias),s.anisotropyEnable,
        bits(s.anisotropyEnable ? s.maxAnisotropy : 1.0f),s.compareEnable,
        uint32_t(s.compareEnable ? s.compareOp : VK_COMPARE_OP_ALWAYS),bits(s.minLod),bits(s.maxLod),
        uint32_t(border ? s.borderColor : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK),s.unnormalizedCoordinates,s.flags};
}
}
