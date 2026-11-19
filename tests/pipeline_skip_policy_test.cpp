#include "nvn/pipeline_skip_policy.h"
#include <cstdlib>
#include <cstdio>
static void check(bool ok){if(!ok)std::abort();}
int main(){
    using namespace nx::nvn;
    PipelineKey opaque{};
    opaque.color_count=1;opaque.color_formats[0]=VK_FORMAT_R8G8B8A8_UNORM;
    opaque.depth_format=VK_FORMAT_D32_SFLOAT;opaque.depth.depth_test=opaque.depth.depth_write=1;opaque.mask.mask[0]=15;
    check(pipeline_draw_skip_candidate(opaque));
    auto key=opaque;key.depth_format=VK_FORMAT_UNDEFINED;check(!pipeline_draw_skip_candidate(key));
    key=opaque;key.depth.depth_test=0;check(!pipeline_draw_skip_candidate(key));
    key=opaque;key.depth.depth_write=0;check(!pipeline_draw_skip_candidate(key));
    key=opaque;key.color_count=0;check(!pipeline_draw_skip_candidate(key));
    key=opaque;key.color_count=2;check(!pipeline_draw_skip_candidate(key));
    key=opaque;key.color.blend_enable[0]=1;check(!pipeline_draw_skip_candidate(key));
    key=opaque;key.mask.mask[0]=7;check(!pipeline_draw_skip_candidate(key));
    key=opaque;key.depth.stencil_test=1;check(!pipeline_draw_skip_candidate(key));
    key=opaque;key.alpha_to_coverage=1;check(!pipeline_draw_skip_candidate(key));
    std::puts("draw skipping preserves UI/captures, shadow, transparent, stencil and deferred passes");
}
