#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nx::nvn {
struct SamplerLod {
    float bias, minimum, maximum, anisotropy;
};
inline SamplerLod sampler_lod(uint32_t filter, float minimum, float maximum, float bias, float anisotropy,
                              bool anisotropy_supported, float max_bias, float max_anisotropy) {
    const float limit = std::isfinite(max_bias) ? std::max(0.0f,max_bias) : 0.0f;
    bias = std::isfinite(bias) ? std::clamp(bias,-limit,limit) : 0.0f;
    minimum = filter <= 1 || !std::isfinite(minimum) ? 0.0f : std::max(0.0f,minimum);
    maximum = filter <= 1 ? 0.25f : std::max(minimum,std::isfinite(maximum) ? maximum : 1000.0f);
    const float cap = anisotropy_supported && std::isfinite(max_anisotropy)
        ? std::clamp(max_anisotropy,1.0f,16.0f) : 1.0f;
    anisotropy = std::isfinite(anisotropy) ? std::clamp(anisotropy,1.0f,cap) : 1.0f;
    return {bias == 0.0f ? 0.0f : bias,minimum == 0.0f ? 0.0f : minimum,maximum,anisotropy};
}
}
