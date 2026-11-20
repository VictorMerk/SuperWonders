#pragma once
#include "netplay_wire.h"
#include <algorithm>
#include <locale>
#include <mutex>
#include <ostream>

namespace nx::lan {
enum class ApplyEvent : unsigned {
    Initial, PipelineSample, PipelineSubmitReturn, PipelineReceive, LeadIn,
    ExchangeComplete, Apply, LoadingBegin, LoadingDrainDiscard, LoadingDrainEmpty,
    LoadingExchange, LoadingNeutralApply, LoadingEnd, HidDelivery, FrameworkFrame, Count
};
inline const char* apply_event_name(ApplyEvent e) {
    constexpr const char* names[]={"initial","pipeline_sample","pipeline_submit_return","pipeline_receive","neutral_lead_in",
        "exchange_complete","apply","loading_begin","loading_drain_discard","loading_drain_empty",
        "loading_exchange","loading_neutral_apply","loading_end","hid_delivery","framework_frame"};
    return names[unsigned(e)];
}
struct ApplyTraceEntry {
    uint64_t sequence=0, ns=0, frame=0, tick=0, sampled_ns=0, local_loading_event=0;
    uint64_t sampling_number=0, caller=0, framework_sequence=0, frame_before=0;
    uint32_t player=0;
    ApplyEvent event=ApplyEvent::Initial;
    bool agreed=false;
    FrameInputs inputs;
};
// Optional evidence only. Retain the session prefix AND a circular recent tail,
// so a long loading barrier cannot erase the first bad application.
// push performs no filesystem work. Capacity and event counts are explicit.
class ApplyTrace {
    mutable std::mutex lock;
    std::vector<ApplyTraceEntry> entries;
    size_t prefix=0, retained=0, tail_next=0;
    uint64_t total=0;
    std::array<uint64_t,unsigned(ApplyEvent::Count)> counts{};
public:
    explicit ApplyTrace(size_t capacity):entries(std::max(size_t(2),capacity)),prefix(entries.size()/2) {}
    void push(ApplyTraceEntry e) {
        std::lock_guard guard(lock);
        e.sequence=++total; ++counts[unsigned(e.event)];
        if(retained<prefix) entries[retained++]=e;
        else {
            entries[prefix+tail_next]=e;
            tail_next=(tail_next+1)%(entries.size()-prefix);
            retained=std::min(retained+1,entries.size());
        }
    }
    void write(std::ostream& out,uint32_t role,uint64_t seed,bool commit_hid_identity=false) const {
        std::lock_guard guard(lock);
        out.imbue(std::locale::classic());
        out << "# schema=1,role=" << role << ",seed=" << seed << ",capacity=" << entries.size()
            << ",total=" << total << ",retained=" << retained << ",omitted=" << total-retained;
        if(commit_hid_identity)out << ",hid_identity=exposed_commit_plus_one";
        out << '\n';
        out << "# scope=application events; monotonic clocks are local; sample precedes queue insertion; apply is exposure to HID, not proof of a guest read or world agreement\n";
        for(unsigned i=0;i<unsigned(ApplyEvent::Count);++i)
            out << "# count_" << apply_event_name(ApplyEvent(i)) << '=' << counts[i] << '\n';
        out << "sequence,event,monotonic_ns,frame,tick,sampled_ns,local_loading_event,agreed,players,player,sampling_number,caller,framework_sequence,frame_before";
        for(size_t p=0;p<kPlayers;++p) out << ",p" << p << "_loading_event,p" << p << "_buttons,p" << p << "_lx,p" << p << "_ly,p" << p << "_rx,p" << p << "_ry";
        out << '\n';
        const auto row=[&](const ApplyTraceEntry& e) {
            out << e.sequence << ',' << apply_event_name(e.event) << ',' << e.ns << ',' << e.frame << ',' << e.tick << ',' << e.sampled_ns
                << ',' << e.local_loading_event << ',' << e.agreed << ',' << unsigned(e.inputs.players) << ',' << e.player << ',' << e.sampling_number << ',' << e.caller << ',' << e.framework_sequence << ',' << e.frame_before;
            for(size_t p=0;p<kPlayers;++p) {
                const auto& pad=e.inputs.pads[p];
                out << ',' << e.inputs.loading_events[p] << ',' << pad.buttons << ',' << pad.lx << ',' << pad.ly << ',' << pad.rx << ',' << pad.ry;
            }
            out << '\n';
        };
        const auto prefix_count=std::min(retained,prefix);
        for(size_t i=0;i<prefix_count;++i) row(entries[i]);
        const auto tail_count=retained-prefix_count;
        const auto start=retained==entries.size()?tail_next:0;
        for(size_t i=0;i<tail_count;++i) row(entries[prefix+(start+i)%(entries.size()-prefix)]);
    }
};
}
