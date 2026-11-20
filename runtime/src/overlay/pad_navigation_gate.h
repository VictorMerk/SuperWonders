#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>

namespace nx::overlay {

// One bounded mailbox. The mutex never covers a host poll, ImGui call, callback,
// logging, or another lock. Menu state and accepted samples share its generation.
class PadNavigationGate {
public:
    struct Sample { uint64_t buttons=0; int32_t lx=0,ly=0; uint64_t physical_buttons=0; };
    struct Ticket { uint64_t generation=0,sequence=0; };
    struct State { uint64_t generation=0; bool open=false; };
    struct Transition { bool changed=false; State state; };
    struct PublishResult { bool accepted=false; Transition transition; };
    struct Frame {
        State state;
        Sample raw,nav;
        uint64_t pressed=0,sample_sequence=0;
        bool sample_valid=false,capturing=false,quarantined=false,navigation_allowed=false;
    };
    static constexpr int32_t kNeutralStickLimit=9830; // 0.3 * Horizon's 32767, rounded down

private:
    mutable std::mutex mutex_;
    std::atomic<bool> open_{false};
    uint64_t generation_=1,issued_=0,published_=0,neutral_after_=0;
    Sample raw_;
    uint64_t raw_sequence_=0,previous_buttons_=0,pressed_=0;
    bool valid_=false,capturing_=false,quarantined_=false,combo_armed_=true;

    State state_locked() const { return {generation_,open_.load(std::memory_order_relaxed)}; }
    static bool neutral(const Sample& p) {
        // Range comparisons also handle INT32_MIN without abs() overflow.
        // Bindings may be applied asynchronously: logical zero is not a physical release.
        return p.buttons==0 && p.physical_buttons==0 && p.lx>=-kNeutralStickLimit && p.lx<=kNeutralStickLimit &&
               p.ly>=-kNeutralStickLimit && p.ly<=kNeutralStickLimit;
    }
    Transition transition_locked(bool open) {
        if(open==open_.load(std::memory_order_relaxed))return {false,state_locked()};
        // Exhaustion fails closed rather than allowing a generation to repeat.
        if(generation_==std::numeric_limits<uint64_t>::max())open=false;
        else ++generation_;
        open_.store(open,std::memory_order_release);
        published_=issued_;
        valid_=false;pressed_=0;capturing_=false;quarantined_=true;
        neutral_after_=issued_;
        // A chord held across a transition cannot toggle the next menu again.
        // Any accepted non-chord sample rearms it, independently of capture.
        combo_armed_=false;
        return {true,state_locked()};
    }

public:
    bool is_open() const noexcept {return open_.load(std::memory_order_acquire);}
    State state() const {std::lock_guard lock(mutex_);return state_locked();}
    uint64_t generation() const {std::lock_guard lock(mutex_);return generation_;}
    Transition set_open(bool open) {std::lock_guard lock(mutex_);return transition_locked(open);}
    Transition toggle() {
        std::lock_guard lock(mutex_);
        return transition_locked(!open_.load(std::memory_order_relaxed));
    }

    // MUST precede the physical poll, not merely publication of an already-read
    // pad: a producer paused before capture ends must not certify fresh neutral.
    Ticket begin_sample() {
        std::lock_guard lock(mutex_);
        if(issued_==std::numeric_limits<uint64_t>::max() || generation_==std::numeric_limits<uint64_t>::max()) {
            valid_=false;pressed_=0;quarantined_=true;return {};
        }
        return {generation_,++issued_};
    }
    PublishResult publish(Ticket ticket,Sample sample,uint64_t combo_mask=0) {
        std::lock_guard lock(mutex_);
        if(ticket.generation!=generation_ || !ticket.sequence || ticket.sequence>issued_ || ticket.sequence<=published_)
            return {false,{false,state_locked()}};
        published_=ticket.sequence;
        const auto previous=previous_buttons_;
        previous_buttons_=sample.buttons;
        raw_=sample;raw_sequence_=ticket.sequence;
        const bool combo=combo_mask && (sample.buttons&combo_mask)==combo_mask;
        if(!combo)combo_armed_=true;
        else if(combo_armed_) {
            // Toggle inside this lock, then return its metadata after unlocking.
            // The caller only logs/notifies; it must not recursively toggle.
            return {true,transition_locked(!open_.load(std::memory_order_relaxed))};
        }
        valid_=true;
        if(capturing_)pressed_=0;
        else if(quarantined_) {
            if(ticket.sequence>neutral_after_ && neutral(sample))quarantined_=false;
            pressed_=0;
        } else pressed_|=sample.buttons&~previous;
        return {true,{false,state_locked()}};
    }
    void set_capture(bool active) {
        std::lock_guard lock(mutex_);
        if(active==capturing_)return;
        capturing_=active;quarantined_=true;pressed_=0;
        // Arm on BOTH boundaries. A neutral observation made while listening,
        // or a poll begun before listening ended, cannot release this barrier.
        neutral_after_=issued_;
    }
    // A preset/reset/direct assignment can change bindings without listening.
    // Call only when effective bindings change, never once per rendered frame.
    void quarantine_navigation() {
        std::lock_guard lock(mutex_);
        quarantined_=true;pressed_=0;neutral_after_=issued_;
    }
    Sample raw() const {std::lock_guard lock(mutex_);return raw_;}
    Frame consume() {
        std::lock_guard lock(mutex_);
        Frame frame;
        frame.state=state_locked();frame.raw=raw_;frame.sample_sequence=raw_sequence_;
        frame.sample_valid=valid_;frame.capturing=capturing_;frame.quarantined=quarantined_;
        // A launcher screen still navigates with settings closed. The caller
        // decides whether the overlay/screen currently takes input.
        frame.navigation_allowed=valid_&&!capturing_&&!quarantined_;
        if(frame.navigation_allowed){frame.nav=raw_;frame.pressed=pressed_;}
        pressed_=0;
        return frame;
    }
};

} // namespace nx::overlay
