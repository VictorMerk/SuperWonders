// The replay's state commands (split from nvn_draw.cpp): binds, viewports, uniform updates, counters; draws and
// dispatches go back to nvn_draw.cpp.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>

#include "nvn_draw_state.h"
#include "platform.h"
#include "render_condition.h"

namespace nx::nvn {

using namespace replay;

namespace {
std::map<uint64_t,std::array<uint64_t,2>> g_report_values;
std::vector<WriteBack> g_report_writes;
void update_pending_reports(const uint8_t* destination, const uint8_t* source, size_t size) {
    const uintptr_t begin = reinterpret_cast<uintptr_t>(destination), end = begin+size;
    for (auto& write : g_report_writes) {
        const uintptr_t report = reinterpret_cast<uintptr_t>(write.to);
        const uintptr_t lo = std::max(begin,report), hi = std::min(end,report+write.size);
        if (lo < hi) std::memcpy(write.from+(lo-report),source+(lo-begin),hi-lo);
    }
    for (auto& [address,value] : g_report_values) {
        const uintptr_t report = uintptr_t(address);
        const uintptr_t lo = std::max(begin,report), hi = std::min(end,report+sizeof value);
        if (lo < hi) std::memcpy(reinterpret_cast<uint8_t*>(value.data())+(lo-report),source+(lo-begin),hi-lo);
    }
}
}
void replay::reports_begin_submit() { g_report_values.clear(); g_report_writes.clear(); }
std::vector<WriteBack> replay::reports_take_write_backs() {
    std::vector<WriteBack> out;
    out.swap(g_report_writes);
    return out;
}

bool replay::rendering_enabled() {
    if (!g_state.render_enable) return false;
    static const bool legacy = std::getenv("NX_LEGACY_RENDER_CONDITION") != nullptr;
    if (legacy || !g_state.render_condition_address) return true;
    const uint8_t* p = guest_bytes(g_state.render_condition_address);
    if (!p || guest_bytes(g_state.render_condition_address+23) != p+23) return true;
    uint64_t a = 0, b = 0;
    std::memcpy(&a,p,sizeof a);
    std::memcpy(&b,p+16,sizeof b);
    // Different GPU pool aliases can name the same report storage.
    if (auto it = g_report_values.find(reinterpret_cast<uintptr_t>(p)); it != g_report_values.end()) a = it->second[0];
    if (auto it = g_report_values.find(reinterpret_cast<uintptr_t>(p+16)); it != g_report_values.end()) b = it->second[0];
    const bool on = conditional_render(g_state.render_condition_mode,a,b);
    if (trace_capture()) log("render condition: mode %u @%llx values %llu / %llu: %s",
        g_state.render_condition_mode,(unsigned long long)g_state.render_condition_address,
        (unsigned long long)a,(unsigned long long)b,on ? "on" : "off");
    return on;
}

void draw_command(VkCommandBuffer cb, const Cmd& c, const CmdList& list, const Targets& targets) {
    State& s = g_state;
    auto blob = [&](size_t size) -> const uint8_t* {
        return c.blob < list.blobs.size() && list.blobs[c.blob].size() >= size ? list.blobs[c.blob].data() : nullptr;
    };
    switch (c.op) {
        case Cmd::SetViewport:
            std::memcpy(s.viewport, c.i, sizeof s.viewport), s.has_viewport = true;
            break;
        case Cmd::SetRasterizerDiscard: s.rasterizer_discard = c.n != 0; break;
        case Cmd::BindImage:
            if (c.i[0] >= 0 && c.i[0] < kStages && c.i[1] >= 0 && c.i[1] < 16) s.images[c.i[0]][c.i[1]] = c.a[0];
            break;
        case Cmd::DispatchCompute: dispatch_call(cb, c); break;
        case Cmd::SetRenderEnable:
            s.render_enable = c.n != 0;
            s.render_condition_address = 0;
            break;
        case Cmd::SetRenderEnableConditional:
            s.render_enable = true;
            s.render_condition_address = c.a[0];
            s.render_condition_mode = c.n;
            break;
        case Cmd::SetScissor:
            std::memcpy(s.scissor, c.i, sizeof s.scissor), s.has_scissor = true;
            break;
        case Cmd::SetDepthRange:
            s.depth_near = c.f[0], s.depth_far = c.f[1];
            break;
        case Cmd::BindProgram: s.program = c.a[0]; break;
        case Cmd::BindUniformBuffer:
            if (c.i[0] >= 0 && c.i[0] < kStages && c.i[1] >= 0 && c.i[1] < 18) s.uniforms[c.i[0]][c.i[1]] = {c.a[0], c.a[1]};
            break;
        case Cmd::BindStorageBuffer:
            if (c.i[0] >= 0 && c.i[0] < kStages && c.i[1] >= 0 && c.i[1] < 16) s.storage[c.i[0]][c.i[1]] = {c.a[0], c.a[1]};
            break;
        case Cmd::BindDeferred: {
            const DeferredValues* dv = current_deferred();
            auto it = dv ? dv->find(c.a[0]) : DeferredValues::const_iterator{};
            uint32_t words = c.mask >= 2 ? 2 : 1;
            int st = c.i[0];
            if (st < 0 || st >= kStages) break;
            for (uint32_t k = 0; k < c.n; k++) {
                auto value = [&](uint32_t w) {  // the submit's snapshot, else the memory now
                    size_t at = size_t(k) * words + w;
                    return dv && it != dv->end() && at < it->second.size() ? it->second[at]
                                                                            : ld<uint64_t>(c.a[0] + 8 * at);
                };
                int slot = c.i[1] + int(k);
                if (c.mask == 0 && slot >= 0 && slot < 32) s.textures[st][slot] = value(0);
                if (c.mask == 1 && slot >= 0 && slot < 16) s.images[st][slot] = value(0);
                if (c.mask == 2 && slot >= 0 && slot < 18) s.uniforms[st][slot] = {value(0), value(1)};
                if (c.mask == 3 && slot >= 0 && slot < 16) s.storage[st][slot] = {value(0), value(1)};
            }
            break;
        }
        case Cmd::BindTexture:
            if (c.i[0] >= 0 && c.i[0] < kStages && c.i[1] >= 0 && c.i[1] < 32) s.textures[c.i[0]][c.i[1]] = c.a[0];
            break;
        case Cmd::BindVertexBuffer:
            if (c.i[0] >= 0 && c.i[0] < 16) s.vertex_buffers[c.i[0]] = {c.a[0], c.a[1]};
            break;
        case Cmd::SetTexturePool: s.texture_pool = c.a[0]; break;
        case Cmd::SetSamplerPool: s.sampler_pool = c.a[0]; break;
        case Cmd::BindVertexAttribs:
            if (auto* p = blob(c.n * sizeof(VertexAttrib)))
                s.attribs.assign(reinterpret_cast<const VertexAttrib*>(p), reinterpret_cast<const VertexAttrib*>(p) + c.n);
            break;
        case Cmd::BindVertexStreams:
            if (auto* p = blob(c.n * sizeof(VertexStream)))
                s.streams.assign(reinterpret_cast<const VertexStream*>(p), reinterpret_cast<const VertexStream*>(p) + c.n);
            break;
        case Cmd::BindBlendState:
            if (auto* p = blob(sizeof(BlendTarget))) {
                BlendTarget b;
                std::memcpy(&b, p, sizeof b);
                if (b.target < 8) s.blend[b.target] = b;
            }
            break;
        case Cmd::BindColorState:
            if (auto* p = blob(sizeof(ColorStateInfo))) std::memcpy(&s.color, p, sizeof s.color);
            break;
        case Cmd::BindChannelMaskState:
            if (auto* p = blob(sizeof(ChannelMaskInfo))) std::memcpy(&s.mask, p, sizeof s.mask);
            break;
        case Cmd::BindDepthStencilState:
            if (auto* p = blob(sizeof(DepthStencilInfo))) std::memcpy(&s.depth, p, sizeof s.depth);
            break;
        case Cmd::BindPolygonState:
            if (auto* p = blob(sizeof(PolygonInfo))) std::memcpy(&s.polygon, p, sizeof s.polygon);
            break;
        case Cmd::BindMultisampleState: s.alpha_to_coverage = c.n != 0; break;
        case Cmd::SetBlendColor: std::memcpy(s.blend_color, c.f, sizeof s.blend_color); break;
        case Cmd::UpdateUniformBuffer: {  // (buffer address, size, offset, data): written in order, like the GPU
            uint64_t off = c.a[2];
            // NX_TRACE_UUB=<first draw>: logs 40 updates from that draw on.
            static const char* uub = std::getenv("NX_TRACE_UUB");
            static const uint64_t uub_from = uub ? std::strtoull(uub, nullptr, 10) : 0;
            static int uub_n = 0;
            if (uub && g_stats.done >= uub_from && uub_n++ < 1500 && c.blob < list.blobs.size()) {
                const auto& b = list.blobs[c.blob];
                float f[8] = {};
                std::memcpy(f, b.data(), std::min<size_t>(b.size(), sizeof f));
                log("uub before draw %llu: buffer %llx size %llu offset %llu data %zu bytes: %g %g %g %g | %g %g %g %g",
                    (unsigned long long)g_stats.done, (unsigned long long)c.a[0], (unsigned long long)c.a[1],
                    (unsigned long long)off, b.size(), f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]);
            }
            if (c.blob < list.blobs.size())
                if (uint8_t* p = const_cast<uint8_t*>(guest_bytes(c.a[0] + off))) {
                    update_pending_reports(p,list.blobs[c.blob].data(),list.blobs[c.blob].size());
                    std::memcpy(p, list.blobs[c.blob].data(), list.blobs[c.blob].size());
                    dedup_invalidate(p, list.blobs[c.blob].size());
                    written_cpu_write(p, list.blobs[c.blob].size());
                }
            break;
        }
        case Cmd::ReportCounter: {  // {value, timestamp}: the work before it is done when the replay gets here
            static uint64_t reports = 0, types_seen = 0;
            if (c.n < 64 && !(types_seen >> c.n & 1)) types_seen |= 1ull << c.n, log("nvn: first counter report of type %u", c.n);
            if (uint8_t* p = const_cast<uint8_t*>(guest_bytes(c.a[0]))) {
                if (guest_bytes(c.a[0]+15) != p+15) break;
                // NX_FAKE_GPU_TIME=1: every report of a submit carries the submit's start (a GPU with no cost):
                // the game picks its present interval from the timings it measures (bisecting its 30 fps mode).
                static const bool fake = std::getenv("NX_FAKE_GPU_TIME") != nullptr;
                static const bool legacy = std::getenv("NX_LEGACY_RENDER_CONDITION") != nullptr;
                const uint64_t serial = ++reports;
                uint64_t v[2] = {legacy ? serial : report_counter_value(c.n,serial),
                                 fake ? submit_start_ns() : plat::monotonic_ns()};
                // Keep command-order values for later conditions, but publish
                // reports to the CPU only after this submit's fence. Early
                // publication lets the recording thread reuse a capture while
                // the GPU is still reading it.
                g_report_values[reinterpret_cast<uintptr_t>(p)] = {v[0],v[1]};
                auto data = upload_alloc(sizeof v,8);
                std::memcpy(data.ptr,v,sizeof v);
                g_report_writes.push_back({static_cast<uint8_t*>(data.ptr),p,sizeof v,{}});
                dedup_invalidate(p, sizeof v);
                written_cpu_write(p, sizeof v);
            }
            break;
        }
        case Cmd::DrawArrays: draw_call(cb, targets, c.n, false, c); break;
        case Cmd::DrawElements: draw_call(cb, targets, c.n, true, c); break;
        default: raster_command(c); break;
    }
}

}  // namespace nx::nvn
