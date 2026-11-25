#pragma once
#include "nvn_pipeline.h"
namespace nx::nvn {
// Recycled attachments do not prove a blur/capture, shadow map, transparent
// layer or deferred pass will repeat. Even the opt-in skipping mode must wait
// for these pipelines. Only ordinary opaque colour+depth draws are eligible.
inline bool pipeline_draw_skip_candidate(const PipelineKey& key) {
    return key.color_count==1 && key.color_formats[0]!=VK_FORMAT_UNDEFINED &&
        key.depth_format!=VK_FORMAT_UNDEFINED && key.depth.depth_test && key.depth.depth_write &&
        !key.depth.stencil_test && !key.alpha_to_coverage && !key.color.blend_enable[0] && key.mask.mask[0]==15;
}
}
