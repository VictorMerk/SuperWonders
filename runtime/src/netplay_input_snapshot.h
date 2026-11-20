#pragma once
#include "netplay_wire.h"
#include <limits>
#include <mutex>

namespace nx::lan {

struct InputReadMetadata {
    bool active=false;
    uint64_t local_loading_event=0;
};
struct InputSnapshot {
    plat::PadState pad;
    uint64_t committed_tick=0, sampling_number=0, local_loading_event=0;
    uint32_t player=0;
    uint8_t players=0;
    bool active=false, connected=false;
};

// The runtime's existing input mutex owns both publication and reads. Network
// exchanges/loading retain their existing locking and cadence. A transport-only
// loading pump is not a publication or a new HID sample.
class InputPublication {
    mutable std::mutex mutex_;
    FrameInputs current_;
    bool initialized_=false;

    static bool valid(const FrameInputs& next) noexcept {
        // Npad sampling is signed s64; zero is reserved for no publication.
        return next.tick<uint64_t(std::numeric_limits<int64_t>::max()) && next.players<=kPlayers;
    }
    static bool same_pad(const plat::PadState& a,const plat::PadState& b) noexcept {
        return a.buttons==b.buttons&&a.lx==b.lx&&a.ly==b.ly&&a.rx==b.rx&&a.ry==b.ry;
    }
public:
    std::mutex& mutex() noexcept {return mutex_;}
    // Caller must hold mutex(), or be initializing before guest/worker startup.
    const FrameInputs& current_locked() const noexcept {return current_;}
    bool reset_locked(const FrameInputs& initial) noexcept {
        if(!valid(initial))return false;
        current_=initial;initialized_=true;return true;
    }
    // Caller holds mutex(). Reject identity reuse with different delivered data;
    // diagnostics/timing can change without changing the HID payload.
    bool publish_locked(const FrameInputs& next) noexcept {
        if(!valid(next) || (initialized_&&next.tick<current_.tick))return false;
        if(initialized_&&next.tick==current_.tick) {
            if(next.players!=current_.players)return false;
            for(size_t i=0;i<kPlayers;++i)if(!same_pad(next.pads[i],current_.pads[i]))return false;
        }
        current_=next;initialized_=true;return true;
    }
    template<class Metadata>
    InputSnapshot read(uint32_t player,Metadata metadata) const {
        std::lock_guard guard(mutex_);
        const InputReadMetadata observed=metadata(); // active/marker share this observation
        InputSnapshot result;
        result.player=player;result.active=observed.active;
        result.local_loading_event=observed.local_loading_event;
        if(!initialized_)return result;
        result.committed_tick=current_.tick;result.sampling_number=current_.tick+1;
        result.players=current_.players;
        result.connected=observed.active&&player<current_.players&&player<kPlayers;
        if(result.connected)result.pad=current_.pads[player];
        return result;
    }
};

} // namespace nx::lan
