#include "nvn/pipeline_blend_key.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace nx::nvn;
static void check(bool pass, const char* why) {
    if (!pass) { std::fprintf(stderr, "%s\n", why); std::exit(1); }
}
static PipelineKey canonical(PipelineKey key) { canonicalize_pipeline_blend(key); return key; }
static bool same(const PipelineKey& a, const PipelineKey& b) { return std::memcmp(&a, &b, sizeof a) == 0; }
int main() {
    PipelineKey base;
    std::memset(&base, 0, sizeof base);
    base.color_count = 2;
    base.color_formats[0] = base.color_formats[1] = VK_FORMAT_R8G8B8A8_UNORM;
    base.mask.mask[0] = base.mask.mask[1] = 15;
    base.color.blend_enable[1] = 1;
    base.blend[1] = {1,5,6,2,1,1,1};
    auto a = canonical(base);
    auto b = base;
    b.blend[0] = {7,0x10,0x11,7,8,4,5};  // disabled: arbitrary old factors cannot change output
    b.blend[1].target = 7;  // routing was resolved before the key was assembled
    b.blend[7] = {7,5,6,7,8,2,3}; b.color.blend_enable[7] = 1; b.mask.mask[7] = 1;
    check(same(a,canonical(b)), "inactive blend state must reuse the pipeline");
    check(same(a,canonical(a)), "canonicalization must be idempotent");
    b = base; b.blend[1].src_rgb = 2;
    check(!same(a,canonical(b)), "active blend factors must stay distinct");
    b = base; b.blend[1].eq_a = 3;
    check(!same(a,canonical(b)), "active alpha equations must stay distinct");
    b = base; b.color.blend_enable[0] = 1;
    check(!same(a,canonical(b)), "enabling blend must stay distinct");
    b = base; b.mask.mask[0] = 7;
    check(!same(a,canonical(b)), "live alpha write masks must stay distinct even without blending");
    b = base; b.color_formats[0] = VK_FORMAT_B8G8R8A8_UNORM;
    check(!same(a,canonical(b)), "attachment formats must stay distinct");
    b = base; b.depth.depth_write = 1;
    check(!same(a,canonical(b)), "depth state must stay intact");
    b = base; b.color_count = 0;
    auto c = canonical(b); b.blend[0] = {2,3,4,5,6,1,2};
    check(same(c,canonical(b)), "depth-only passes must ignore colour blend state");
    for (uint32_t count = 1; count <= 8; ++count) {
        auto key = base; key.color_count = count;
        for (uint32_t i = 0; i < count; ++i) {
            key.color.blend_enable[i] = 1;
            key.blend[i] = {i,5,6,2,1,1,1};
            key.mask.mask[i] = 15;
        }
        const auto reference = canonical(key);
        for (uint32_t i = 0; i < count; ++i) {
            auto changed = key; changed.blend[i].dst_rgb = 2;
            check(!same(reference,canonical(changed)), "every live attachment's blend factors must survive");
            changed = key; changed.mask.mask[i] = 3;
            check(!same(reference,canonical(changed)), "every live attachment's write mask must survive");
        }
    }
    std::puts("pipeline blend identity: inactive state merges; active factors, masks, formats and depth remain distinct");
}
