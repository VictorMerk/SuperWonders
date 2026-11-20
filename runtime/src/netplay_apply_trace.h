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
    LoadingExchange, LoadingNeutralApply, LoadingEnd, HidDelivery, FrameworkFrame, FrameworkEnter, FrameworkExit, FrameworkThrow, LoadingFile, Count
};
inline const char* apply_event_name(ApplyEvent e) {
    constexpr const char* names[]={"initial","pipeline_sample","pipeline_submit_return","pipeline_receive","neutral_lead_in",
        "exchange_complete","apply","loading_begin","loading_drain_discard","loading_drain_empty",
        "loading_exchange","loading_neutral_apply","loading_end","hid_delivery","framework_frame","framework_enter","framework_exit","framework_throw","loading_file"};
    return names[unsigned(e)];
}
struct ApplyTraceEntry {
    uint64_t sequence=0, ns=0, frame=0, tick=0, sampled_ns=0, local_loading_event=0;
    uint64_t sampling_number=0, caller=0, framework_sequence=0, frame_before=0;
    uint64_t invocation=0, parent_invocation=0, resource_hash=0, caller_offset=0;
    uint32_t thread_ordinal=0, caller_module=0; // 0 unknown; module 1 main, 2 sdk
    uint32_t player=0;
    ApplyEvent event=ApplyEvent::Initial;
    bool agreed=false;
    FrameInputs inputs;
};
// Fixed-size local diagnostic identities. Raw guest TLS tokens stay in RAM and
// are never exported. Exhausted/unknown threads and nested overflow fail unknown.
struct ObservationIdentity { uint64_t invocation=0, parent=0; uint32_t thread=0; };
class ObservationRegistry {
    struct Thread { uint64_t token=0; std::array<uint64_t,8> stack{}; size_t depth=0, overflow=0; };
    std::array<Thread,64> threads_{};
    std::mutex mutex_;
    uint64_t next_=0;
    Thread* find(uint64_t token) {
        if(!token)return nullptr;
        for(auto& thread:threads_)if(thread.token==token)return &thread;
        for(auto& thread:threads_)if(!thread.token){thread.token=token;return &thread;}
        return nullptr;
    }
    uint32_t ordinal(const Thread* thread) const {return thread?uint32_t(thread-threads_.data()+1):0;}
public:
    ObservationIdentity begin(uint64_t token) {
        std::lock_guard lock(mutex_);
        if(next_==UINT64_MAX)return {};
        const uint64_t id=++next_;auto* thread=find(token);
        ObservationIdentity result{id,0,ordinal(thread)};
        if(thread) {
            if(!thread->overflow&&thread->depth)result.parent=thread->stack[thread->depth-1];
            if(thread->depth<thread->stack.size()&&!thread->overflow)thread->stack[thread->depth++]=id;
            else {++thread->overflow;result.parent=0;}
        }
        return result;
    }
    ObservationIdentity current(uint64_t token) {
        std::lock_guard lock(mutex_);auto* thread=find(token);
        return {thread&&thread->depth&&!thread->overflow?thread->stack[thread->depth-1]:0,0,ordinal(thread)};
    }
    void end(uint64_t token,uint64_t invocation) {
        std::lock_guard lock(mutex_);auto* thread=find(token);
        if(!thread)return;
        if(thread->overflow){--thread->overflow;return;}
        if(thread->depth&&thread->stack[thread->depth-1]==invocation)--thread->depth;
    }
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
        out << ",observation_identity=local_invocation_v1";
        out << '\n';
        out << "# scope=application events; monotonic clocks are local; sample precedes queue insertion; apply is exposure to HID, not proof of a guest read or world agreement; observations use local invocation/thread ordinals, guest module-relative callers (not host callers), and noncryptographic resource hashes; file notification is before guest API return, not loader readiness\n";
        for(unsigned i=0;i<unsigned(ApplyEvent::Count);++i)
            out << "# count_" << apply_event_name(ApplyEvent(i)) << '=' << counts[i] << '\n';
        out << "sequence,event,monotonic_ns,frame,tick,sampled_ns,local_loading_event,agreed,players,player,sampling_number,caller,framework_sequence,frame_before";
        for(size_t p=0;p<kPlayers;++p) out << ",p" << p << "_loading_event,p" << p << "_buttons,p" << p << "_lx,p" << p << "_ly,p" << p << "_rx,p" << p << "_ry";
        out << ",invocation,parent_invocation,thread_ordinal,resource_hash,caller_module,caller_offset\n";
        const auto row=[&](const ApplyTraceEntry& e) {
            out << e.sequence << ',' << apply_event_name(e.event) << ',' << e.ns << ',' << e.frame << ',' << e.tick << ',' << e.sampled_ns
                << ',' << e.local_loading_event << ',' << e.agreed << ',' << unsigned(e.inputs.players) << ',' << e.player << ',' << e.sampling_number << ',' << e.caller << ',' << e.framework_sequence << ',' << e.frame_before;
            for(size_t p=0;p<kPlayers;++p) {
                const auto& pad=e.inputs.pads[p];
                out << ',' << e.inputs.loading_events[p] << ',' << pad.buttons << ',' << pad.lx << ',' << pad.ly << ',' << pad.rx << ',' << pad.ry;
            }
            out << ',' << e.invocation << ',' << e.parent_invocation << ',' << e.thread_ordinal << ',' << e.resource_hash << ',' << e.caller_module << ',' << e.caller_offset << '\n';
        };
        const auto prefix_count=std::min(retained,prefix);
        for(size_t i=0;i<prefix_count;++i) row(entries[i]);
        const auto tail_count=retained-prefix_count;
        const auto start=retained==entries.size()?tail_next:0;
        for(size_t i=0;i<tail_count;++i) row(entries[prefix+(start+i)%(entries.size()-prefix)]);
    }
};
}
