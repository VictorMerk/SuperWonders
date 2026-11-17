#pragma once
#include <cstdint>
#include <vector>

namespace nx::nvn {
// Maxwell's ballots and indexed shuffles operate within logical groups of 32 lanes, even on
// a host with 64/128-lane subgroups. No Vulkan dependency; false leaves `out` untouched.
bool patch_warp32(const std::vector<uint32_t>& in, std::vector<uint32_t>& out);
}
