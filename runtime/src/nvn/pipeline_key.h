#pragma once

#include "pipeline_blend_key.h"
#include <cstddef>
#include <type_traits>

namespace nx::nvn {

// Keep the command-blob and pipeline-recipe layout compatible with existing
// files, while making every depth-state byte a defined value.
static_assert(sizeof(DepthStencilInfo) == 40 && offsetof(DepthStencilInfo,depth_func) == 4);
static_assert(std::has_unique_object_representations_v<DepthStencilInfo>);

inline void canonicalize_pipeline_key(PipelineKey& key) {
    canonicalize_pipeline_blend(key);
    key.depth.reserved = 0;  // legacy command blobs / persisted recipes may contain arbitrary padding here
}

}  // namespace nx::nvn
