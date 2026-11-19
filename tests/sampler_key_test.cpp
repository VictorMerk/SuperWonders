#include "sampler_key.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
static void check(bool ok) { if(!ok) std::abort(); }
int main() {
    using nx::nvn::sampler_key;
    for(uint32_t mode:{2u,3u,4u}) {
        check(nx::nvn::sampler_wrap(mode,false)==VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        check(nx::nvn::sampler_wrap(mode,true)==VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE);
    }
    VkSamplerCreateInfo a{},b{};
    std::memset(&a,0xAA,sizeof a);std::memset(&b,0x55,sizeof b);
    a.minFilter=b.minFilter=VK_FILTER_LINEAR;a.magFilter=b.magFilter=VK_FILTER_NEAREST;
    a.mipmapMode=b.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
    a.addressModeU=b.addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT;
    a.addressModeV=b.addressModeV=a.addressModeW=b.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    a.mipLodBias=0;b.mipLodBias=-0.0f;a.anisotropyEnable=b.anisotropyEnable=0;
    a.compareEnable=b.compareEnable=0;a.minLod=b.minLod=0;a.maxLod=b.maxLod=32;
    a.unnormalizedCoordinates=b.unnormalizedCoordinates=0;a.flags=b.flags=0;
    a.sType=b.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;a.pNext=b.pNext=nullptr;
    a.maxAnisotropy=16;b.maxAnisotropy=2;
    a.compareOp=VK_COMPARE_OP_LESS;b.compareOp=VK_COMPARE_OP_GREATER;
    a.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;b.borderColor=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    // Padding, pNext, sType and disabled/unused fields are not sampler state.
    check(sampler_key(a)==sampler_key(b));
    b.minFilter=VK_FILTER_NEAREST;check(sampler_key(a)!=sampler_key(b));b.minFilter=a.minFilter;
    b.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;check(sampler_key(a)!=sampler_key(b));
    a=b;a.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;b.borderColor=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    check(sampler_key(a)!=sampler_key(b));
    a=b;a.compareEnable=b.compareEnable=1;a.compareOp=VK_COMPARE_OP_LESS;b.compareOp=VK_COMPARE_OP_GREATER;
    check(sampler_key(a)!=sampler_key(b));
    std::puts("sampler key: padding, signed zero and ignored fields reuse; effective state stays distinct");
}
