#pragma once
#include "netplay_wire.h"
#include <cmath>

namespace nx::lan {
// The discovery probe reads live guest memory. Reject an unusable observation
// locally instead of sending a malformed position that tears down the room.
inline bool usable_probe_position(std::span<const float,3> position) {
    for(float value:position) if(!std::isfinite(value) || std::abs(value)>1e6f) return false;
    return true;
}
// Heap discovery has no actor lifetime/slot identity. Its first observation
// cannot establish divergence. Require an agreeing baseline for each scene,
// and confirmation of a later disagreement, without weakening explicit hashes.
class PositionProbeGuard {
    uint64_t scene=0;
    unsigned matching=0, differing=0;
    bool armed=false;
public:
    void reset() { scene=0; matching=differing=0; armed=false; }
    bool ready() const { return armed; }
    bool observe(const PlayerState& a,const PlayerState& b,unsigned warmup) {
        if(!a.valid || !b.valid) { reset(); return true; }
        if(!warmup) { armed=true; return compatible_state(a,b); }
        if(a.scene!=b.scene) { reset(); return true; }
        if(scene!=a.scene) { reset(); scene=a.scene; }
        const bool same=compatible_state(a,b);
        if(!armed) {
            matching=same?matching+1:0;
            armed=matching>=warmup;
            return true;
        }
        differing=same?0:differing+1;
        return differing<3;
    }
};
}
