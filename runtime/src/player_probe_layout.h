#pragma once
#include <cstdint>
#include <string_view>
namespace nx::play {
struct PlayerProbeLayout {
    uint64_t vt=0;
    uint32_t pos=0,vel=0;
};
inline PlayerProbeLayout verified_player_layout(std::string_view build,uint64_t module_base) {
    if(build=="cd6e42aee7934f4d8393cd43893ae25edc83d338000000000000000000000000")
        return {module_base+0x96e400,0xc00,0};
    // Product.120's frame/RNG hooks are ported; its actor layout has not been
    // verified. Never scan for an original-build address in a different binary.
    return {};
}
}
