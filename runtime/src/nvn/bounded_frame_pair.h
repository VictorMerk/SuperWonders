#pragma once
#include <cstdint>
#include <limits>

namespace nx::nvn::diagnostic {
// A diagnostic-only one-shot sequence, not a renderer scheduling policy.
// Guest-frame and presentation counters may have different origins.
class BoundedFramePair {
public:
    enum class Step { Idle, Capture, Mismatch };
    static constexpr uint32_t kMaxFrames=6;
    bool begin(uint64_t frame,uint64_t present,uint32_t count=kMaxFrames) {
        if(attempted_)return false;
        attempted_=true;
        const auto max=std::numeric_limits<uint64_t>::max();
        if(!count || count>kMaxFrames || frame>max-count || present>max-count)return false;
        frame_=frame;present_=present;remaining_=count;return true;
    }
    Step next(uint64_t frame,uint64_t present) {
        if(!remaining_)return Step::Idle;
        if(frame!=frame_+1 || present!=present_+1) {
            remaining_=0;failed_=true;return Step::Mismatch;
        }
        frame_=frame;present_=present;--remaining_;return Step::Capture;
    }
    void cancel(){remaining_=0;failed_=true;}
    bool active() const{return remaining_!=0;}
    bool failed() const{return failed_;}
    uint32_t remaining() const{return remaining_;}
private:
    uint64_t frame_=0,present_=0;
    uint32_t remaining_=0;
    bool attempted_=false,failed_=false;
};
} // namespace nx::nvn::diagnostic
