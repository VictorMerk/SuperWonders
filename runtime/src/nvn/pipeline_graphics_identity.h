#pragma once
#include "nvn_pipeline.h"
#include "pipeline_key.h"

namespace nx::nvn {
using GraphicsPipelineIdentity=ByteKey<sizeof(PipelineKey)+48>;
// Input shader hashes have already passed graphics_pipeline's 16-byte check.
// This is the actual runtime key builder, shared with CPU compatibility tests.
inline GraphicsPipelineIdentity graphics_pipeline_identity(const PipelineKey& key,PipelineKey& state_key) {
    GraphicsPipelineIdentity identity{};
    std::memcpy(identity.b,key.vs->hash.data(),16);
    if(key.fs)std::memcpy(identity.b+16,key.fs->hash.data(),16);
    std::memcpy(&state_key,&key,sizeof key);state_key.vs=state_key.fs=nullptr;
    canonicalize_pipeline_key(state_key);
    std::memcpy(identity.b+32,&state_key,sizeof state_key);
    const uint64_t variants[2]={int_texture_variant_id(key.vs),key.fs?int_texture_variant_id(key.fs):0};
    std::memcpy(identity.b+32+sizeof state_key,variants,sizeof variants);
    return identity;
}
}
