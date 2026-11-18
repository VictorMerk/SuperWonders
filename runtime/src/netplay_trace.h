#pragma once
#include "netplay_wire.h"
#include <iomanip>
#include <locale>
#include <mutex>
#include <ostream>

namespace nx::lan {
// Diagnostic evidence, not a whole-world checksum or replay implementation.
// No filesystem access in push(). Keep the most recent 4096 exchanges in RAM.
struct TraceEntry {
    uint64_t tick = 0, frame = 0, wait_ns = 0, partial_hash = 0, sampled_ns = 0;
    bool committed = false;
    plat::PadState local;
    PlayerState state;
    FrameInputs agreed;
};
template<size_t Capacity = 4096> class InputTrace {
    static_assert(Capacity > 0);
    std::array<TraceEntry, Capacity> entries{};
    size_t next = 0, count = 0;
    mutable std::mutex lock;
public:
    void reset() { std::lock_guard guard(lock); next = count = 0; }
    void push(const TraceEntry& entry) {
        std::lock_guard guard(lock);
        entries[next] = entry; next = (next + 1) % Capacity;
        if (count < Capacity) ++count;
    }
    void write(std::ostream& out, uint32_t role, uint64_t seed, uint8_t input_lead=0) const {
        std::lock_guard guard(lock);
        out.imbue(std::locale::classic());
        out << "tick,frame,role,seed,committed,wait_ns,partial_hash,state_valid,scene,x,y,z,local_buttons,local_lx,local_ly,local_rx,local_ry,players,sampled_ns,input_lead";
        for (size_t p = 0; p < kPlayers; ++p)
            out << ",p" << p << "_buttons,p" << p << "_lx,p" << p << "_ly,p" << p << "_rx,p" << p << "_ry";
        out << '\n' << std::setprecision(9);
        for (size_t i = 0; i < count; ++i) {
            const auto& e = entries[(next + Capacity - count + i) % Capacity];
            out << e.tick << ',' << e.frame << ',' << role << ',' << seed << ',' << e.committed << ',' << e.wait_ns
                << ',' << e.partial_hash << ',' << e.state.valid << ',' << e.state.scene;
            for (float pos : e.state.position) out << ',' << pos;
            const auto pad = [&](const plat::PadState& p) { out << ',' << p.buttons << ',' << p.lx << ',' << p.ly << ',' << p.rx << ',' << p.ry; };
            pad(e.local); out << ',' << unsigned(e.agreed.players) << ',' << e.sampled_ns << ',' << unsigned(input_lead);
            for (const auto& p : e.agreed.pads) pad(p);
            out << '\n';
        }
    }
};
}
