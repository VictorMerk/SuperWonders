#include "pipeline_miss_trace.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace nx::nvn::pipeline_trace {
namespace {
const char* operation_name(Operation op) {
    switch(op) {
    case Operation::Miss:return "lookup_miss";
    case Operation::CacheProbe:return "cache_probe";
    case Operation::AsyncQueued:return "async_queued";
    case Operation::AsyncWait:return "async_wait";
    case Operation::AsyncObserved:return "async_completion_observed";
    case Operation::SyncCompile:return "sync_compile";
    case Operation::GplLibrariesAndLink:return "gpl_libraries_and_link";
    case Operation::DrawSkipped:return "draw_skipped_pipeline_pending";
    }
    return "unknown";
}
struct State {
    std::string path;
    Recorder recorder{4096};
    std::atomic_flag stopped = ATOMIC_FLAG_INIT;
    explicit State(const char* p):path(p) {}
};
State* state() noexcept {
    // Process-lifetime storage: detached pipeline workers may outlive C++ statics.
    static State* value=[]() noexcept -> State* {
        const char* path=std::getenv("NX_PIPELINE_MISS_TRACE");
        if(!path || !*path || std::string_view(path)=="0")return nullptr;
        try {return new State(path);} catch(...) {return nullptr;}
    }();
    return value;
}
}
Recorder::Recorder(size_t capacity):ring_(capacity) {}
void Recorder::capture(Event event) noexcept {
    try {
        std::lock_guard lock(mutex_);
        if(frozen_)return;
        event.sequence=++total_;
        if(ring_.empty())return;
        ring_[next_]=event;
        next_=(next_+1)%ring_.size();
        size_=std::min(size_+1,ring_.size());
    } catch(...) {} // Diagnostics must not change pipeline behavior.
}
void Recorder::freeze_and_write(std::ostream& out) {
    std::lock_guard lock(mutex_);
    frozen_=true;
    out<<"# schema=1,total_events="<<total_<<",retained_events="<<size_
       <<",evicted_events="<<(total_-size_)<<",capacity="<<ring_.size()<<'\n';
    out<<"# scope=host_pipeline_operations;observed_ns=steady_clock;frame=observation_only;key_fingerprint=noncryptographic_fnv1a64_includes_process_local_type_ids_do_not_compare_alone_across_runs;durations_overlap;async_completion_observed_duration=worker_compile_not_wait;cache_probe_result_1000297000=compile_required;missing_optional_probes=unknown_not_hit;no_shader_bytes\n";
    out<<"sequence,observed_ns,frame,key_fingerprint,vs_hash,fs_hash,caller_may_skip,effective_may_skip,gpl,operation,duration_ns,result_known,result\n";
    const size_t first=size_==ring_.size()?next_:0;
    for(size_t i=0;i<size_;++i) {
        const auto& e=ring_[(first+i)%ring_.size()];
        out<<e.sequence<<','<<e.observed_ns<<','<<e.identity.frame<<','<<e.identity.key<<','
           <<e.identity.vs<<','<<e.identity.fs<<','<<int(e.identity.caller_may_skip)<<','
           <<int(e.identity.effective_may_skip)<<','<<int(e.identity.gpl)<<','
           <<operation_name(e.operation)<<','<<e.duration_ns<<','<<int(e.result_known)<<',';
        if(e.result_known)out<<e.result;
        out<<'\n';
    }
}
uint64_t fingerprint(std::span<const std::byte> bytes) noexcept {
    uint64_t value=14695981039346656037ull;
    for(auto byte:bytes) {value^=uint8_t(byte);value*=1099511628211ull;}
    return value;
}
Identity identity(uint64_t key,uint64_t frame,std::string_view vs,std::string_view fs,
                  bool caller,bool effective,bool gpl) noexcept {
    Identity id;id.key=key;id.frame=frame;id.caller_may_skip=caller;id.effective_may_skip=effective;id.gpl=gpl;
    // Shader identity is hexadecimal only; never emit arbitrary strings/paths.
    const auto copy=[](char* out,std::string_view text) {
        if(text.size()!=16)return;
        for(char c:text)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return;
        std::copy(text.begin(),text.end(),out);
    };
    copy(id.vs,vs);copy(id.fs,fs);return id;
}
bool enabled() noexcept {return state()!=nullptr;}
uint64_t now_ns() noexcept {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
void capture(const Identity& id,Operation op,uint64_t duration,int32_t result,bool known) noexcept {
    if(auto* s=state();s && !s->stopped.test(std::memory_order_relaxed))
        s->recorder.capture({id,op,0,now_ns(),duration,result,known});
}
void shutdown() noexcept {
    auto* s=state();if(!s || s->stopped.test_and_set())return;
    try {
        std::ofstream out(std::filesystem::path(reinterpret_cast<const char8_t*>(s->path.c_str())),std::ios::trunc);
        if(out)s->recorder.freeze_and_write(out);
    } catch(...) {} // Export failure cannot interrupt normal process shutdown.
}
}
