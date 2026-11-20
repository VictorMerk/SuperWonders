// NVN command-buffer binds and state setters beyond the single ones in nvn_state.cpp (docs/NVN.md, N6): the plural
// forms (and their "Deferred" variants, which bind the same way here) record one single bind per element, so the
// replay needs nothing new. Signatures from Wonder's call sites (nvn_trace.txt):
//   BindTextures[Deferred](cmd, stage, first, count, const NVNtextureHandle*)
//   BindUniformBuffers[Deferred] / BindStorageBuffers[Deferred](cmd, stage, first, count, const NVNbufferRange*)
//     with NVNbufferRange {u64 address; u64 size}
//   BindVertexBuffers(cmd, first, count, const NVNbufferRange*)
//   SetViewports(cmd, first, count, const float* {x, y, w, h}), SetScissors(cmd, first, count, const int* {x, y, w, h})
//   SetDepthRanges(cmd, first, count, const float* {near, far})
//   SetRasterizerDiscard(cmd, NVNboolean), SetRenderEnable(cmd, NVNboolean)
// Only viewport/scissor/depth range 0 are replayed (one viewport).
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "nvn.h"
#include "deferred_bind_policy.h"
#include "nvn_draw_state.h"
#include "kernel.h"

namespace nx::nvn {

namespace {

void bind_ranges(CPU& c, Cmd::Op op) {
    int32_t stage = int32_t(c.x[1]), first = int32_t(c.x[2]), count = int32_t(c.x[3]);
    for (int32_t k = 0; k < count && c.x[4]; k++) {
        Cmd cmd{op};
        cmd.i[0] = stage, cmd.i[1] = first + k;
        cmd.a[0] = ld<uint64_t>(c.x[4] + 16ull * k), cmd.a[1] = ld<uint64_t>(c.x[4] + 16ull * k + 8);
        static int terrain_logs = 0;
        const char* terrain_trigger = std::getenv("NX_TERRAIN_BIND_TRACE");
        FILE* terrain_file = terrain_trigger && terrain_logs < 8 && op == Cmd::BindUniformBuffer && stage == 0 && first + k == 4 && cmd.a[1] == 57408 ? std::fopen(terrain_trigger, "rb") : nullptr;
        if (terrain_file) {
            std::fclose(terrain_file);
            ++terrain_logs;
            log("terrain bind plural: address=%llx size=%llu lr=%s", (unsigned long long)cmd.a[0], (unsigned long long)cmd.a[1], describe(c.x[30]).c_str());
            backtrace(c);
        }
        record(c.x[0], cmd);
    }
}

// The Deferred forms keep the array's address: the driver reads it when the commands are submitted, and Wonder fills
// its (double-buffered) handle arrays after recording. Read at recording, the scene sampled the AO, ambient-light and
// shadow textures of two frames earlier: dark actors, a white sky (docs/nvn/DEBUG.md #41).
void BindTextures(CPU& c);
void BindImages(CPU& c);
void BindUniformBuffers(CPU& c);
void BindStorageBuffers(CPU& c);

// Existing NX_DEFERRED_BINDS keeps its presence-based opt-in for all four kinds.
// NX_DEFERRED_TEXTURE_BINDS=1 is a separate, default-off texture-only candidate;
// it leaves image and buffer bindings, and every ordinary API, unchanged.
template <uint32_t Kind>
void bind_deferred(CPU& c) {
    static const bool global = std::getenv("NX_DEFERRED_BINDS") != nullptr;
    static const bool textures_only = [] {const char* e=std::getenv("NX_DEFERRED_TEXTURE_BINDS");return e && std::strcmp(e,"1")==0;}();
    const deferred_bind::Request request{int32_t(c.x[1]),int32_t(c.x[2]),int32_t(c.x[3]),c.x[4]};
    deferred_bind::dispatch(Kind,request,global,textures_only,[&] {
        if (Kind == 0) BindTextures(c);
        if (Kind == 1) BindImages(c);
        if (Kind == 2) BindUniformBuffers(c);
        if (Kind == 3) BindStorageBuffers(c);
    },[&](const deferred_bind::Request& r) {
        Cmd cmd{Cmd::BindDeferred};
        cmd.mask=Kind;cmd.i[0]=r.stage;cmd.i[1]=r.first;
        cmd.n=uint32_t(r.count);cmd.a[0]=r.address;
        static const bool origin=std::getenv("NX_TRACE_DEFERRED_TEXTURES")!=nullptr;
        if(Kind==0 && origin && r.stage==1 && r.first>=0 && r.first<=9 && r.count<=32 && r.count>9-r.first) {
            cmd.a[1]=ld<uint64_t>(r.address+8ull*uint32_t(9-r.first));
            cmd.a[2]=g_frame.load(std::memory_order_relaxed);cmd.a[3]=0x44544639; // bounded slot9 provenance
        }
        record(c.x[0],cmd);
    });
}

void BindUniformBuffers(CPU& c) { bind_ranges(c, Cmd::BindUniformBuffer); }
void BindStorageBuffers(CPU& c) { bind_ranges(c, Cmd::BindStorageBuffer); }

void BindTextures(CPU& c) {
    int32_t stage = int32_t(c.x[1]), first = int32_t(c.x[2]), count = int32_t(c.x[3]);
    for (int32_t k = 0; k < count && c.x[4]; k++) {
        Cmd cmd{Cmd::BindTexture};
        cmd.i[0] = stage, cmd.i[1] = first + k;
        cmd.a[0] = ld<uint64_t>(c.x[4] + 8ull * k);
        record(c.x[0], cmd);
    }
}

void BindVertexBuffers(CPU& c) {
    int32_t first = int32_t(c.x[1]), count = int32_t(c.x[2]);
    for (int32_t k = 0; k < count && c.x[3]; k++) {
        Cmd cmd{Cmd::BindVertexBuffer};
        cmd.i[0] = first + k;
        cmd.a[0] = ld<uint64_t>(c.x[3] + 16ull * k), cmd.a[1] = ld<uint64_t>(c.x[3] + 16ull * k + 8);
        record(c.x[0], cmd);
    }
}

void SetViewports(CPU& c) {
    if (c.x[1] != 0 || int32_t(c.x[2]) < 1 || !c.x[3]) return;  // viewport 0 only
    Cmd cmd{Cmd::SetViewport};
    for (int k = 0; k < 4; k++) cmd.i[k] = int32_t(std::lround(ld<float>(c.x[3] + 4ull * k)));
    record(c.x[0], cmd);
    static std::atomic<int> logged{0};
    if ((cmd.i[3] < 0 || cmd.i[2] < 0) && logged++ < 10)
        log("nvn: SetViewports with a negative size: %d %d %d %d", cmd.i[0], cmd.i[1], cmd.i[2], cmd.i[3]);
}

void SetScissors(CPU& c) {
    if (c.x[1] != 0 || int32_t(c.x[2]) < 1 || !c.x[3]) return;
    Cmd cmd{Cmd::SetScissor};
    for (int k = 0; k < 4; k++) cmd.i[k] = ld<int32_t>(c.x[3] + 4ull * k);
    record(c.x[0], cmd);
}

void SetDepthRanges(CPU& c) {
    if (c.x[1] != 0 || int32_t(c.x[2]) < 1 || !c.x[3]) return;
    Cmd cmd{Cmd::SetDepthRange};
    cmd.f[0] = ld<float>(c.x[3]), cmd.f[1] = ld<float>(c.x[3] + 4);
    record(c.x[0], cmd);
}

void SetRasterizerDiscard(CPU& c) {
    Cmd cmd{Cmd::SetRasterizerDiscard};
    cmd.n = uint32_t(c.x[1] & 1);
    record(c.x[0], cmd);
}

void SetRenderEnable(CPU& c) {
    Cmd cmd{Cmd::SetRenderEnable};
    cmd.n = uint32_t(c.x[1] & 1);
    record(c.x[0], cmd);
    static std::atomic<uint64_t> counts[2];
    uint64_t n = ++counts[cmd.n];
    if (n == 1 || n % 5000 == 0)
        log("nvn: SetRenderEnable(%u) x%llu", cmd.n, (unsigned long long)n);
}

// Preserve the report comparison until replay; earlier reports and CPU updates
// in that stream may change its result after this command was recorded.
void SetRenderEnableConditional(CPU& c) {
    Cmd cmd{Cmd::SetRenderEnableConditional};
    cmd.n = uint32_t(c.x[1]);
    cmd.a[0] = c.x[2];
    record(c.x[0], cmd);
}

// BindImage(cmd, stage, index, NVNimageHandle), BindImages[Deferred](cmd, stage, first, count, const NVNimageHandle*)
void BindImage(CPU& c) {
    Cmd cmd{Cmd::BindImage};
    cmd.i[0] = int32_t(c.x[1]), cmd.i[1] = int32_t(c.x[2]);
    cmd.a[0] = c.x[3];
    record(c.x[0], cmd);
}

void BindImages(CPU& c) {
    int32_t stage = int32_t(c.x[1]), first = int32_t(c.x[2]), count = int32_t(c.x[3]);
    for (int32_t k = 0; k < count && c.x[4]; k++) {
        Cmd cmd{Cmd::BindImage};
        cmd.i[0] = stage, cmd.i[1] = first + k;
        cmd.a[0] = ld<uint64_t>(c.x[4] + 8ull * k);
        record(c.x[0], cmd);
    }
}

// DispatchCompute(cmd, int x, int y, int z); DispatchComputeIndirect(cmd, NVNbufferAddress of {u32 x, y, z}),
// read at replay (the GPU may have written it).
void DispatchCompute(CPU& c) {
    Cmd cmd{Cmd::DispatchCompute};
    cmd.i[0] = int32_t(c.x[1]), cmd.i[1] = int32_t(c.x[2]), cmd.i[2] = int32_t(c.x[3]);
    record(c.x[0], cmd);
}

void DispatchComputeIndirect(CPU& c) {
    Cmd cmd{Cmd::DispatchCompute};
    cmd.n = 1;
    cmd.a[0] = c.x[1];
    record(c.x[0], cmd);
}

}  // namespace

NX_HLE("nvnCommandBufferBindImage", BindImage);
NX_HLE("nvnCommandBufferBindImages", BindImages);
NX_HLE("nvnCommandBufferBindImagesDeferred", bind_deferred<1>);
NX_HLE("nvnCommandBufferDispatchCompute", DispatchCompute);
NX_HLE("nvnCommandBufferDispatchComputeIndirect", DispatchComputeIndirect);
NX_HLE("nvnCommandBufferBindUniformBuffers", BindUniformBuffers);
NX_HLE("nvnCommandBufferBindUniformBuffersDeferred", bind_deferred<2>);
NX_HLE("nvnCommandBufferBindStorageBuffers", BindStorageBuffers);
NX_HLE("nvnCommandBufferBindStorageBuffersDeferred", bind_deferred<3>);
NX_HLE("nvnCommandBufferBindTextures", BindTextures);
NX_HLE("nvnCommandBufferBindTexturesDeferred", bind_deferred<0>);
NX_HLE("nvnCommandBufferBindVertexBuffers", BindVertexBuffers);
NX_HLE("nvnCommandBufferSetViewports", SetViewports);
NX_HLE("nvnCommandBufferSetScissors", SetScissors);
NX_HLE("nvnCommandBufferSetDepthRanges", SetDepthRanges);
NX_HLE("nvnCommandBufferSetRasterizerDiscard", SetRasterizerDiscard);
NX_HLE("nvnCommandBufferSetRenderEnable", SetRenderEnable);
NX_HLE("nvnCommandBufferSetRenderEnableConditional", SetRenderEnableConditional);

}  // namespace nx::nvn

namespace nx::nvn {

// The arrays of every Deferred bind in the submitted lists (and the lists they call), read now.
DeferredValues deferred_snapshot(const std::vector<std::shared_ptr<CmdList>>& lists) {
    DeferredValues v;
    std::unordered_map<uint64_t,uint32_t> spans;
    struct Origin {uint64_t recorded=0,frame=0;int first=0;uint32_t count=0;};
    std::unordered_map<uint64_t,Origin> origins;
    std::vector<std::pair<std::shared_ptr<CmdList>, int>> todo;
    for (auto& l : lists) todo.push_back({l, 0});
    while (!todo.empty()) {
        auto [l, depth] = todo.back();
        todo.pop_back();
        if (!l) continue;
        for (const Cmd& c : l->cmds) {
            if (c.op == Cmd::CallList && depth < 8) {
                todo.push_back({command_list(c.a[0]), depth + 1});
            } else if (c.op == Cmd::BindDeferred) {
                // Gather the full word span BEFORE reading anything: a later
                // command can use more handles, or two-word buffer ranges, at
                // the same address (also through a nested command list).
                deferred_bind::note_span(spans,c.a[0],c.mask,c.n);
                if(c.mask==0 && c.a[3]==0x44544639)
                    origins.try_emplace(c.a[0],Origin{c.a[1],c.a[2],c.i[1],c.n});
            }
        }
    }
    const bool observe=!origins.empty() && replay::paired_shadow_observation_window();
    for(const auto& [address,words]:spans) {
        auto vals=deferred_bind::capture_span(words,[&](size_t k){return ld<uint64_t>(address+8*k);});
        if(observe)if(auto it=origins.find(address);it!=origins.end()) {
            const auto& origin=it->second;
            const size_t slot=size_t(9-origin.first);
            static std::atomic<unsigned> logs{0};
            if(slot<vals.size() && origin.recorded!=vals[slot] && logs.fetch_add(1,std::memory_order_relaxed)<8)
                log("nvn: deferred texture origin guest %llu recorded-frame %llu stage 1 slot 9 array %llx first %d count %u snapshot-words %u recorded %llx submitted %llx changed 1",
                    (unsigned long long)g_frame.load(),(unsigned long long)origin.frame,(unsigned long long)address,
                    origin.first,origin.count,words,(unsigned long long)origin.recorded,(unsigned long long)vals[slot]);
        }
        v.emplace(address,std::move(vals));
    }
    return v;
}

}  // namespace nx::nvn
