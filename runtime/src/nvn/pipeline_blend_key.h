#pragma once

#include "nvn_pipeline.h"

namespace nx::nvn {

// Cache identity only: these fields cannot affect the pipeline's colour output.
// Keep enabled blending, attachment formats and write masks on live targets intact.
inline void canonicalize_pipeline_blend(PipelineKey& key) {
    for (uint32_t i = 0; i < 8; ++i) {
        if (i >= key.color_count) {
            key.blend[i] = {};
            key.color.blend_enable[i] = 0;
            key.mask.mask[i] = 0;
        } else {
            key.blend[i].target = 0;  // decoded NVN routing metadata; the array index selects the target
            if (!key.color.blend_enable[i]) key.blend[i] = {};
        }
    }
}

}  // namespace nx::nvn
