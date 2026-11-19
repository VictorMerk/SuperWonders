// NVN sampler states as Vulkan samplers (split from nvn_pipeline.cpp).
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>

#include "nvn_pipeline.h"
#include "texture_content_hash.h"
#include "sampler_limits.h"
#include "sampler_key.h"

namespace nx::nvn {

using namespace nx::vk;

namespace {

VkSamplerAddressMode vk_wrap(uint32_t w) {  // NVN: 0 clamp, 1 repeat, 2 mirror clamp, 3 mirror clamp to edge,
    if (w >= 2 && w <= 4 && !vk_mirror_clamp()) {
        static bool logged = false;
        if (!logged) { logged=true; log("nvn/vk: mirror-clamp unavailable; using clamp-to-edge sampler fallback"); }
    }
    return sampler_wrap(w,vk_mirror_clamp());
}
uint64_t g_sampler_creations = 0;

std::atomic<int> g_anisotropy{0};  // the player's setting: 0 the game's own, else 2, 4, 8 or 16

// Anisotropic filtering raised to the player's level (NX_ANISOTROPY=<0|2|4|8|16> overrides the setting). Latched at
// the first sampler: samplers are cached per texture slot for the session, so a change applies after a restart.
float anisotropy_floor() {
    static const float level = [] {
        int a = g_anisotropy.load();
        if (const char* e = std::getenv("NX_ANISOTROPY")) a = std::atoi(e);
        a = a >= 16 ? 16 : a >= 8 ? 8 : a >= 4 ? 4 : a >= 2 ? 2 : 0;
        if (a && !vk_features().samplerAnisotropy) {
            log("nvn/vk: anisotropic filtering %dx asked, the GPU has none", a);
            return 0.0f;
        }
        if (a) log("nvn/vk: anisotropic filtering at least %dx on mipmapped linear samplers", a);
        return float(a);
    }();
    return level;
}

}  // namespace

void set_anisotropy(int level) { g_anisotropy = level; }

// ---- samplers -------------------------------------------------------------------------------------------------------

template<bool UseFront>
static VkSampler sampler_for_impl(const SamplerInfo& s, bool shadow) {
    // A bounded raw-state front avoids building and hashing normalized state for
    // repeated draw bindings. Full comparison makes index collisions harmless.
    struct Front { SamplerInfo state; bool shadow=false; VkSampler sampler=VK_NULL_HANDLE; };
    static Front recent[64];
    Front& front=recent[(s.min_filter ^ s.mag_filter*7u ^ s.wrap[0]*11u ^ s.wrap[1]*13u ^ uint32_t(shadow)*31u)&63];
    if constexpr (UseFront)
        if(front.sampler && front.shadow==shadow && std::memcmp(&front.state,&s,sizeof s)==0) return front.sampler;
    using Key = ByteKey<16*sizeof(uint32_t)>;
    static std::unordered_map<Key, VkSampler, ByteKeyHash> cache;
    VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    // NVN min filter: 0 nearest, 1 linear, 2 nearest mip nearest, 3 linear mip nearest, 4 nearest mip linear,
    // 5 linear mip linear; mag: 0 nearest, 1 linear.
    uint32_t m = s.min_filter;
    ci.minFilter = (m == 1 || m == 3 || m == 5) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.magFilter = s.mag_filter == 1 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.mipmapMode = (m == 4 || m == 5) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = vk_wrap(s.wrap[0]);
    ci.addressModeV = vk_wrap(s.wrap[1]);
    ci.addressModeW = vk_wrap(s.wrap[2]);
    static const VkPhysicalDeviceLimits limits = [] {
        VkPhysicalDeviceProperties properties{};
        p_vkGetPhysicalDeviceProperties(vk_physical(), &properties);
        return properties.limits;
    }();
    // Games pass lod_min -1000 ("no limit"); Metal wants lodMinClamp >= 0, and a negative one made the glyph
    // sampler return 0 (all text invisible).
    // The game's anisotropy, raised to the player's level for mipmapped linear sampling (never for nearest
    // filtering: pixel-art and lookup textures stay exact).
    float aniso = s.max_anisotropy;
    if (ci.minFilter == VK_FILTER_LINEAR && ci.magFilter == VK_FILTER_LINEAR && m >= 2) aniso = std::max(aniso, anisotropy_floor());
    const auto lod = sampler_lod(m,s.lod_min,s.lod_max,s.lod_bias,aniso,vk_features().samplerAnisotropy,
                                 limits.maxSamplerLodBias,limits.maxSamplerAnisotropy);
    ci.mipLodBias = lod.bias; ci.minLod = lod.minimum; ci.maxLod = lod.maximum;
    ci.anisotropyEnable = lod.anisotropy > 1.0f;
    ci.maxAnisotropy = lod.anisotropy;
    ci.compareEnable = shadow;
    ci.compareOp = shadow ? vk_compare(s.compare_func) : VK_COMPARE_OP_ALWAYS;
    bool white = s.border[3] > 0.5f && s.border[0] > 0.5f;
    ci.borderColor = white ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                     : s.border[3] > 0.5f ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK
                                          : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    const auto fields=sampler_key(ci);
    Key key; std::memcpy(key.b,fields.data(),sizeof key.b);
    auto& v=cache[key];
    if (!v) {
        const VkResult result=p_vkCreateSampler(vk_device(), &ci, nullptr, &v);
        if (result!=VK_SUCCESS) {
            v=VK_NULL_HANDLE;
            static unsigned failures=0;
            if(failures++<8) log("nvn/vk: sampler creation failed (%d); leaving request retryable",int(result));
            return v;
        }
        ++g_sampler_creations;
    }
    if constexpr (UseFront) front={s,shadow,v};
    return v;
}
VkSampler sampler_for(const SamplerInfo& s, bool shadow) { return sampler_for_impl<true>(s,shadow); }

bool sampler_state_probe() {
    bool ok=true; unsigned cases=0;
    for(uint32_t filter=0;filter<6;++filter) for(uint32_t wrap=0;wrap<8;++wrap) for(bool shadow:{false,true}) {
        SamplerInfo s;
        s.min_filter=filter;s.mag_filter=filter&1;s.wrap[0]=s.wrap[1]=s.wrap[2]=wrap;
        s.lod_min=-1000;s.lod_max=1000;s.lod_bias=1000;s.max_anisotropy=1000;
        ok=(sampler_for(s,shadow)!=VK_NULL_HANDLE)&&ok;++cases;
    }
    SamplerInfo s;s.min_filter=5;
    s.lod_min=s.lod_max=s.lod_bias=s.max_anisotropy=std::numeric_limits<float>::quiet_NaN();
    ok=(sampler_for(s,false)!=VK_NULL_HANDLE)&&ok;++cases;
    s={};const VkSampler a=sampler_for(s,false);const auto created=g_sampler_creations;
    s.lod_min=19;s.lod_max=127;s.border[0]=s.border[3]=1;
    const VkSampler b=sampler_for(s,false);
    ok=ok && a && a==b && g_sampler_creations==created;++cases;
    if (std::getenv("NX_SAMPLER_BENCH")) {
        SamplerInfo states[8];
        for(unsigned i=0;i<8;++i) {
            states[i].min_filter=i%6;states[i].mag_filter=i&1;
            states[i].wrap[0]=i%3;states[i].wrap[1]=i/3;
            ok=ok && sampler_for_impl<true>(states[i],false) && sampler_for_impl<false>(states[i],false);
        }
        const auto warmed=g_sampler_creations;
        static volatile uintptr_t sink=0;
        for(bool use_front:{false,true,true,false,false,true,true,false}) {
            constexpr unsigned iterations=200000;
            uintptr_t value=0;
            const auto started=plat::monotonic_ns();
            for(unsigned i=0;i<iterations;++i) {
                const auto sampler=use_front ? sampler_for_impl<true>(states[i&7],false)
                                             : sampler_for_impl<false>(states[i&7],false);
                value^=uintptr_t(sampler)+i;
            }
            const auto elapsed=plat::monotonic_ns()-started;sink=value;
            log("sampler bench: %s %u lookups %.3f ns each",use_front ? "front" : "canonical",iterations,double(elapsed)/iterations);
        }
        ok=ok && g_sampler_creations==warmed;
    }
    log("sampler probe: %u cases, effective-state reuse and device limits: %s",cases,ok ? "PASS" : "FAIL");
    return ok;
}

// ---- pitch-linear texture contents (moved from nvn_pipeline.cpp) ------------------------------------------------

// A pitch-linear texture the GPU does not write: did the CPU change it since the last upload?
void check_linear_content(Texture& t) {
    // Every 4th presented frame at most (every 4th submit was about every frame, ~1 ms); a new glyph shows 3 frames late.
    uint64_t frame = g_frame.load(std::memory_order_relaxed) + 1;
    static const uint64_t hash_small = [] {
        const char* e = std::getenv("NX_HASH_SMALL");
        return e ? std::strtoull(e, nullptr, 10) : 0;
    }();
    bool small = hash_small && t.storage_size <= hash_small;
    if ((!t.stride && !small) || !t.memory || !t.image || t.image->rendered ||
        (t.hashed_submit && t.hashed_submit + (t.watched ? 1 : 4) > frame))
        return;
    t.hashed_submit = frame;
    // 1 MB and more (the glyph cache): page protection says whether the CPU wrote it, every frame, at no cost.
    if (t.stride && t.storage_size >= (1u << 20)) {
        if (!t.watched && !t.content_hash_valid) t.watched = plat::write_watch_add(t.memory, t.storage_size);
        if (t.watched) {
            if (plat::write_watch_take(t.memory) && t.content_hash_valid) {
                t.dirty = true;
                static uint64_t n = 0;
                if (++n == 1 || n % 100 == 0) log("nvn: watched texture written by the CPU (%llu times)", (unsigned long long)n);
            }
            t.content_hash_valid = true;  // the first take only arms the watch (the image was just uploaded)
            return;
        }
    }
    uint64_t h = texture_content_hash(reinterpret_cast<const uint8_t*>(t.memory), t.storage_size);
    if (update_texture_content_hash(h,t.content_hash,t.content_hash_valid)) {
        t.dirty = true;
        static int logged = 0;
        if (!t.stride && logged++ < 100)
            log("nvn: block-linear texture %llux%u f%x (%llu bytes) changed by the CPU", (unsigned long long)t.width,
                t.height, t.format, (unsigned long long)t.storage_size);
    }
}

}  // namespace nx::nvn
