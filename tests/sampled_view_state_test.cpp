#include "sampled_view_state.h"
#include <cstdlib>
#include <cstdio>
static void check(bool ok) { if(!ok) std::abort(); }
int main() {
    using namespace nx::nvn;
    for(uint32_t value=0;value<256;++value) {
        const auto s=sampled_swizzle(value,VK_COMPONENT_SWIZZLE_IDENTITY);
        check(value<6 ? s>=VK_COMPONENT_SWIZZLE_ZERO && s<=VK_COMPONENT_SWIZZLE_A : s==VK_COMPONENT_SWIZZLE_IDENTITY);
    }
    check(!sampled_range(0,12,0,0,0,0,VK_IMAGE_VIEW_TYPE_2D_ARRAY));
    check(!sampled_range(3,12,0,0,11,0,VK_IMAGE_VIEW_TYPE_CUBE));
    check(!sampled_range(3,12,0,0,7,0,VK_IMAGE_VIEW_TYPE_CUBE_ARRAY));
    auto cube=sampled_range(3,12,0,0,6,0,VK_IMAGE_VIEW_TYPE_CUBE);
    check(cube && (*cube)[2]==6 && (*cube)[3]==6);
    auto one=sampled_range(3,13,0,1,1,1,VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    auto all=sampled_range(3,13,0,1,1,65537,VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    auto exact=sampled_range(3,13,0,1,1,12,VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    check(one && all && exact && *one!=*all && *all==*exact);
    auto wide=sampled_range(3,70001,0,1,1,70000,VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    check(wide && (*wide)[3]==70000);
    std::puts("sampled view state: cube bounds, full-width canonical ranges and all swizzle bytes pass");
}
