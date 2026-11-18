// Optical-flow interpolation adapted from adriadam10/mkw-recomp's vk_framegen.c (MIT).
// Its shaders follow SkyboundTotk, Copyright (c) 2026 NotRexed (MIT).
// See third_party/mkw-framegen. This is picture-based interpolation, not FSR 3 or DLSS.
#include "nvn_vk.h"
#include "present_geometry.h"
#include "framegen_spv.h"
#include "platform.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <cstdio>

namespace nx::nvn {
using namespace nx::vk;
namespace {
std::atomic<bool> enabled{false};
std::atomic<int> multiplier{2};
struct State {
    bool tried = false, ready = false, history = false;
    uint32_t width = 0, height = 0, failed_width = 0, failed_height = 0, failed_factor = 0, current = 0, next_set = 0;
    uint64_t last = 0, generated = 0;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline luma = VK_NULL_HANDLE, coarse = VK_NULL_HANDLE, fine = VK_NULL_HANDLE;
    VkPipeline full = VK_NULL_HANDLE, median = VK_NULL_HANDLE, synth = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    static constexpr uint32_t kSets = 256;
    std::array<VkDescriptorSet, kSets> sets{};
    std::array<GpuImage, 2> picture{}, luma_full{}, quarter{}, sixteenth{};
    std::array<GpuImage, 6> flow{};
    std::array<GpuImage, 7> middle{};
    uint32_t factor = 0;
} state;

void barrier(VkCommandBuffer cb, bool between_compute_passes = false) {
    static const bool probe_compute = std::getenv("NX_FRAMEGEN_PROBE") && std::getenv("NX_FRAMEGEN_PROBE_COMPUTE_BARRIERS");
    const bool compute = between_compute_passes && probe_compute;
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    b.srcAccessMask = compute ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = compute ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    const VkPipelineStageFlags stage = compute ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    p_vkCmdPipelineBarrier(cb, stage, stage,
                          0, 1, &b, 0, nullptr, 0, nullptr);
}
void free_image(GpuImage& image) {
    const auto device = vk_device();
    if (image.view) p_vkDestroyImageView(device, image.view, nullptr);
    if (image.image) p_vkDestroyImage(device, image.image, nullptr);
    if (image.memory) {
        p_vkFreeMemory(device, image.memory, nullptr);
        gpu_image_memory_freed(image.allocation_size);
    }
    image = {};
}
void free_images() {
    for (auto* images : {&state.picture, &state.luma_full, &state.quarter, &state.sixteenth})
        for (auto& image : *images) free_image(image);
    for (auto& image : state.flow) free_image(image);
    for (auto& image : state.middle) free_image(image);
    state.factor = 0;
    state.width = state.height = 0; state.history = false;
}
bool image(GpuImage& out, uint32_t width, uint32_t height, VkFormat format) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = format; ci.extent = {width, height, 1};
    ci.mipLevels = ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const auto device = vk_device();
    if (p_vkCreateImage(device, &ci, nullptr, &out.image) != VK_SUCCESS) return false;
    VkMemoryRequirements req{}; p_vkGetImageMemoryRequirements(device, out.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size; ai.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (p_vkAllocateMemory(device, &ai, nullptr, &out.memory) != VK_SUCCESS) return false;
    out.allocation_size = req.size; gpu_image_memory_allocated(req.size);
    if (p_vkBindImageMemory(device, out.image, out.memory, 0) != VK_SUCCESS) return false;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = out.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (p_vkCreateImageView(device, &vi, nullptr, &out.view) != VK_SUCCESS) return false;
    out.width = out.logical_width = width; out.height = out.logical_height = height;
    out.format = format; out.mutable_format = false;
    return true;
}
VkPipeline pipeline(const uint32_t* code, size_t bytes, const int* values = nullptr, uint32_t count = 0) {
    // QA only: compare compiled shader variants in one otherwise identical
    // executable. Normal gameplay always uses the embedded modules.
    std::vector<uint32_t> probe_code;
    if (std::getenv("NX_FRAMEGEN_PROBE")) {
        const char* key = code == k_fg_flow_comp ? "NX_FRAMEGEN_PROBE_FLOW_SPV"
                        : code == k_fg_synth_comp ? "NX_FRAMEGEN_PROBE_SYNTH_SPV"
                        : code == k_fg_luma_comp ? "NX_FRAMEGEN_PROBE_LUMA_SPV"
                        : code == k_fg_median_comp ? "NX_FRAMEGEN_PROBE_MEDIAN_SPV" : nullptr;
        const char* path = key ? std::getenv(key) : nullptr;
        if (path && *path) {
            FILE* f = plat::fopen_utf8(path,"rb");
            bool ok = false;
            if (f) {
                plat::fseek64(f,0,SEEK_END);
                const int64_t size = plat::ftell64(f);
                if (size >= 20 && size <= (4ll << 20) && size % 4 == 0) {
                    probe_code.resize(size_t(size)/4);
                    plat::fseek64(f,0,SEEK_SET);
                    ok = std::fread(probe_code.data(),1,size_t(size),f) == size_t(size) && !std::ferror(f);
                    ok = ok && probe_code[0] == 0x07230203 && probe_code[3] != 0 && probe_code[4] == 0;
                }
                std::fclose(f);
            }
            if (!ok) { log("framegen probe: invalid shader override %s",key); return VK_NULL_HANDLE; }
            code = probe_code.data(); bytes = probe_code.size()*4;
            log("framegen probe: %s override loaded (%zu bytes)",key,bytes);
        }
    }
    VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mi.codeSize = bytes; mi.pCode = code;
    VkShaderModule module = VK_NULL_HANDLE;
    if (p_vkCreateShaderModule(vk_device(), &mi, nullptr, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
    std::array<VkSpecializationMapEntry, 7> map{};
    for (uint32_t i = 0; i < count; ++i) map[i] = {i, i*4, 4};
    VkSpecializationInfo spec{count, map.data(), size_t(count)*4, values};
    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; ci.stage.module = module; ci.stage.pName = "main";
    if (count) ci.stage.pSpecializationInfo = &spec;
    ci.layout = state.layout;
    VkPipeline result = VK_NULL_HANDLE;
    p_vkCreateComputePipelines(vk_device(), VK_NULL_HANDLE, 1, &ci, nullptr, &result);
    p_vkDestroyShaderModule(vk_device(), module, nullptr);
    return result;
}
bool init() {
    if (state.tried) return state.ready;
    state.tried = true;
    for (auto format : {VK_FORMAT_R16_SFLOAT, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM}) {
        VkFormatProperties props{}; p_vkGetPhysicalDeviceFormatProperties(vk_physical(), format, &props);
        const auto needed = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((props.optimalTilingFeatures & needed) != needed) {
            log("nvn/vk: optical-flow frame generation unavailable: format %d", int(format)); return false;
        }
    }
    VkDescriptorSetLayoutBinding bindings[4]{};
    for (uint32_t i = 0; i < 4; ++i)
        bindings[i] = {i, i < 3 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                       1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 4; li.pBindings = bindings;
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
    VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pi.setLayoutCount = 1; pi.pSetLayouts = &state.set_layout; pi.pushConstantRangeCount = 1; pi.pPushConstantRanges = &range;
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.minFilter = si.magFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, State::kSets*3},
                                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, State::kSets}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = State::kSets; pool.poolSizeCount = 2; pool.pPoolSizes = sizes;
    const auto device = vk_device();
    if (p_vkCreateDescriptorSetLayout(device, &li, nullptr, &state.set_layout) != VK_SUCCESS ||
        p_vkCreatePipelineLayout(device, &pi, nullptr, &state.layout) != VK_SUCCESS ||
        p_vkCreateSampler(device, &si, nullptr, &state.sampler) != VK_SUCCESS ||
        p_vkCreateDescriptorPool(device, &pool, nullptr, &state.pool) != VK_SUCCESS) return false;
    std::array<VkDescriptorSetLayout, State::kSets> layouts; layouts.fill(state.set_layout);
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = state.pool; ai.descriptorSetCount = State::kSets; ai.pSetLayouts = layouts.data();
    if (p_vkAllocateDescriptorSets(device, &ai, state.sets.data()) != VK_SUCCESS) return false;
    const int coarse[] = {2,4,8,0,1,0,0}, fine[] = {4,4,3,1,1,0,1}, full[] = {16,4,2,1,2,1,0};
    state.luma = pipeline(k_fg_luma_comp, sizeof k_fg_luma_comp);
    state.coarse = pipeline(k_fg_flow_comp, sizeof k_fg_flow_comp, coarse, 7);
    state.fine = pipeline(k_fg_flow_comp, sizeof k_fg_flow_comp, fine, 7);
    state.full = pipeline(k_fg_flow_comp, sizeof k_fg_flow_comp, full, 7);
    state.median = pipeline(k_fg_median_comp, sizeof k_fg_median_comp);
    state.synth = pipeline(k_fg_synth_comp, sizeof k_fg_synth_comp);
    state.ready = state.luma && state.coarse && state.fine && state.full && state.median && state.synth;
    log("nvn/vk: optical-flow frame generation pipelines %s", state.ready ? "ready" : "unavailable");
    return state.ready;
}
bool ensure_images(uint32_t width, uint32_t height, uint32_t factor) {
    if (state.width == width && state.height == height && state.factor == factor) return true;
    if (state.failed_width == width && state.failed_height == height && state.failed_factor == factor) return false;
    p_vkDeviceWaitIdle(vk_device()); free_images();
    const uint32_t qw = (width+3)/4, qh = (height+3)/4, gw = (width+15)/16, gh = (height+15)/16;
    bool ok = true;
    for (uint32_t i = 0; i < 2 && ok; ++i)
        ok = image(state.picture[i], width, height, VK_FORMAT_R8G8B8A8_UNORM) &&
             image(state.luma_full[i], width, height, VK_FORMAT_R16_SFLOAT) &&
             image(state.quarter[i], qw, qh, VK_FORMAT_R16_SFLOAT) &&
             image(state.sixteenth[i], gw, gh, VK_FORMAT_R16_SFLOAT);
    for (uint32_t i = 0; i < 6 && ok; ++i)
        ok = image(state.flow[i], i < 2 ? (gw+1)/2 : gw, i < 2 ? (gh+1)/2 : gh, VK_FORMAT_R16G16_SFLOAT);
    for (uint32_t i = 0; i+1 < factor && ok; ++i)
        ok = image(state.middle[i], width, height, VK_FORMAT_R8G8B8A8_UNORM);
    if (!ok) {
        free_images(); state.failed_width = width; state.failed_height = height; state.failed_factor = factor;
        log("nvn/vk: frame generation disabled for %ux%u: image allocation failed", width, height); return false;
    }
    state.width = width; state.height = height; state.current = 0; state.factor = factor;
    log("nvn/vk: optical-flow history allocated: %ux%u", width, height);
    return true;
}
void pass(VkCommandBuffer cb, VkPipeline program, GpuImage& a, GpuImage& b, GpuImage& c, GpuImage& out, const void* pc) {
    const VkDescriptorSet set = state.sets[state.next_set++ % State::kSets];
    VkDescriptorImageInfo images[4] = {{state.sampler,a.view,VK_IMAGE_LAYOUT_GENERAL},
        {state.sampler,b.view,VK_IMAGE_LAYOUT_GENERAL}, {state.sampler,c.view,VK_IMAGE_LAYOUT_GENERAL},
        {VK_NULL_HANDLE,out.view,VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet writes[4]{};
    for (uint32_t i = 0; i < 4; ++i) {
        auto& w = writes[i]; w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = set; w.dstBinding = i; w.descriptorCount = 1;
        w.descriptorType = i < 3 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w.pImageInfo = &images[i];
    }
    p_vkUpdateDescriptorSets(vk_device(), 4, writes, 0, nullptr);
    transition(cb, a, VK_IMAGE_LAYOUT_GENERAL); transition(cb, b, VK_IMAGE_LAYOUT_GENERAL);
    transition(cb, c, VK_IMAGE_LAYOUT_GENERAL); transition(cb, out, VK_IMAGE_LAYOUT_GENERAL);
    p_vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, program);
    p_vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, state.layout, 0, 1, &set, 0, nullptr);
    p_vkCmdPushConstants(cb, state.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, pc);
    p_vkCmdDispatch(cb, (out.width+7)/8, (out.height+7)/8, 1);
    barrier(cb,true);
}
}

void set_framegen(bool on, int factor) { multiplier = std::clamp(factor,2,8); enabled = on; }
bool framegen_enabled() {
    static const int forced = [] { const char* e = std::getenv("NX_FRAMEGEN"); return e ? (e[0] != '0') : -1; }();
    return forced < 0 ? enabled.load() : forced != 0;
}
bool framegen_record(VkCommandBuffer cb, GpuImage& src, const int32_t* crop, FramegenFrames& frames) {
    frames = {};
    const uint32_t factor = uint32_t(multiplier.load());
    if (!framegen_enabled()) { state.history = false; return false; }
    if (src.format != VK_FORMAT_R8G8B8A8_UNORM && src.format != VK_FORMAT_B8G8R8A8_UNORM) {
        state.history = false; return false;
    }
    if (!init()) return false;
    int x = 0, y = 0, w = int(src.width), h = int(src.height);
    if (valid_picture_crop(crop, src.logical_width ? src.logical_width : src.width,
                           src.logical_height ? src.logical_height : src.height)) {
        const float sx = float(src.width)/float(src.logical_width ? src.logical_width : src.width);
        const float sy = float(src.height)/float(src.logical_height ? src.logical_height : src.height);
        x = int(std::lround(crop[0]*sx)); y = int(std::lround(crop[1]*sy));
        w = int(std::lround(crop[2]*sx)); h = int(std::lround(crop[3]*sy));
    }
    x = std::clamp(x, 0, int(src.width)); y = std::clamp(y, 0, int(src.height));
    w = std::clamp(w, 0, int(src.width)-x); h = std::clamp(h, 0, int(src.height)-y);
    if (!w || !h || !ensure_images(uint32_t(w), uint32_t(h), factor)) return false;
    const uint64_t now = plat::monotonic_ns();
    const bool interpolate = state.history && now > state.last && now-state.last < 100'000'000;
    state.last = now;
    const uint32_t cur = state.current, prev = cur^1;
    barrier(cb);
    transition(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition(cb, state.picture[cur], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit blit{};
    blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
    blit.srcOffsets[0] = {x,y,0}; blit.srcOffsets[1] = {x+w,y+h,1}; blit.dstOffsets[1] = {w,h,1};
    p_vkCmdBlitImage(cb, src.image, src.layout, state.picture[cur].image, state.picture[cur].layout, 1, &blit, VK_FILTER_NEAREST);
    barrier(cb);
    const int colour[4] = {1,1,0,0}, level[4] = {0,4,0,0};
    auto& pic = state.picture[cur]; auto& lf = state.luma_full[cur];
    auto& lq = state.quarter[cur]; auto& le = state.sixteenth[cur];
    pass(cb,state.luma,pic,pic,pic,lf,colour);
    pass(cb,state.luma,lf,lf,lf,lq,level);
    pass(cb,state.luma,lq,lq,lq,le,level);
    if (interpolate) {
        const float pc16[4] = {16,.0001f,.003f,0}, pc4[4] = {4,.0001f,.003f,0}, pc1[4] = {1,.0001f,.003f,0};
        auto& f = state.flow;
        pass(cb,state.coarse,state.sixteenth[prev],le,le,f[0],pc16);
        pass(cb,state.median,f[0],f[0],f[0],f[1],level);
        pass(cb,state.fine,state.quarter[prev],lq,f[1],f[2],pc4);
        pass(cb,state.median,f[2],f[2],f[2],f[3],level);
        pass(cb,state.full,state.luma_full[prev],lf,f[3],f[4],pc1);
        pass(cb,state.median,f[4],f[4],f[4],f[5],level);
        for (uint32_t i = 0; i+1 < factor; ++i) {
            const float pc[4] = {float(i+1)/float(factor),1.0f/(16.0f*le.width),1.0f/(16.0f*le.height),0};
            pass(cb,state.synth,state.picture[prev],pic,f[5],state.middle[i],pc);
            frames.images[frames.count++] = &state.middle[i];
        }
        state.generated += factor-1;
        if ((state.generated-(factor-1))/600 != state.generated/600 || state.generated == factor-1)
            log("nvn/vk: optical-flow interpolation %ux: %llu frames generated", factor, (unsigned long long)state.generated);
    }
    frames.images[frames.count++] = &pic; state.history = true; state.current ^= 1;
    return true;
}
void framegen_release(bool destroy_pipelines) {
    if (!state.tried) return;
    p_vkDeviceWaitIdle(vk_device()); free_images();
    state.last = 0; state.failed_width = state.failed_height = state.failed_factor = 0;
    if (!destroy_pipelines) return;  // window resizing keeps size-independent compute pipelines
    const auto device = vk_device();
    for (auto program : {state.luma,state.coarse,state.fine,state.full,state.median,state.synth})
        if (program) p_vkDestroyPipeline(device,program,nullptr);
    if (state.pool) p_vkDestroyDescriptorPool(device,state.pool,nullptr);
    if (state.sampler) p_vkDestroySampler(device,state.sampler,nullptr);
    if (state.layout) p_vkDestroyPipelineLayout(device,state.layout,nullptr);
    if (state.set_layout) p_vkDestroyDescriptorSetLayout(device,state.set_layout,nullptr);
    state = {};
}

// Real Vulkan regression, using original synthetic pictures only. The boot probe
// submits and waits between cases so resizing also exercises history retirement.
namespace {
struct Probe {
    GpuImage source;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkQueryPool timing = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint8_t* mapped = nullptr;
    VkBuffer flow_buffer = VK_NULL_HANDLE;
    VkDeviceMemory flow_memory = VK_NULL_HANDLE;
    uint8_t* flow_mapped = nullptr;
    std::array<size_t,7> flow_offsets{};
    std::vector<uint8_t> a, b;
    std::vector<std::vector<uint8_t>> expected;
    uint32_t factor = 2;
    bool recorded = false, still = false, cut = false, compare_only = false;
} probe;
}
bool framegen_probe_record(VkCommandBuffer cb, uint32_t variant) {
    probe = {};
    uint32_t width = variant >= 3 ? 1280 : variant == 1 ? 160 : 192;
    uint32_t height = variant >= 3 ? 720 : variant == 1 ? 96 : 128;
    if (variant >= 3) {
        uint32_t w = 0, h = 0;
        const char* size = std::getenv("NX_FRAMEGEN_PROBE_SIZE");
        if (size && std::sscanf(size, "%ux%u", &w, &h) == 2 && w >= 64 && w <= 4096 && h >= 64 && h <= 4096)
            width = w, height = h;
    }
    const size_t bytes = size_t(width)*height*4;
    if (!image(probe.source,width,height,VK_FORMAT_R8G8B8A8_UNORM)) return false;
    auto colour = [](int x, uint32_t y) {
        uint32_t h = uint32_t(std::max(0,x)) + y*0x9E3779B9u;
        h = (h^(h>>16))*0x85EBCA6Bu; h = (h^(h>>13))*0xC2B2AE35u; h ^= h>>16;
        return std::array<uint8_t,4>{uint8_t(32+(h&127)),uint8_t(64+((h>>8)&127)),uint8_t(32+((h>>16)&127)),255};
    };
    const char* pattern = std::getenv("NX_FRAMEGEN_PROBE_PATTERN");
    const bool diagonal = pattern && (std::strcmp(pattern,"diagonal") == 0 || std::strcmp(pattern,"fractional") == 0);
    const bool fractional = pattern && std::strcmp(pattern,"fractional") == 0;
    const bool hud = pattern && std::strcmp(pattern,"hud") == 0;
    const bool occlusion = pattern && std::strcmp(pattern,"occlusion") == 0;
    const bool flat_new = pattern && std::strcmp(pattern,"flat-new") == 0;
    const bool flat_old = pattern && std::strcmp(pattern,"flat-old") == 0;
    const bool palette = pattern && std::strcmp(pattern,"palette") == 0;
    if (pattern && *pattern && !(diagonal || hud || occlusion || flat_new || flat_old || palette)) {
        log("framegen probe: unknown picture pattern"); return false;
    }
    probe.compare_only = std::getenv("NX_FRAMEGEN_PROBE_COMPARE_ONLY") || flat_new || flat_old || palette;
    if (pattern && *pattern)
        log("framegen probe pattern: %s%s",pattern,probe.compare_only ? " (opaque readback only; compare against reference hashes)" : "");
    auto picture = [&](float shift) {
        std::vector<uint8_t> pixels(bytes);
        // Exact integer horizontal shifts need no interpolation. Keep the QA
        // generator cheap enough that broad shader comparisons spend their time
        // on the GPU checks rather than millions of redundant host round calls.
        if (!diagonal && !hud && !occlusion && shift == std::floor(shift)) {
            const int dx = int(shift);
            for (uint32_t y=0;y<height;++y) for (uint32_t x=0;x<width;++x) {
                const auto c = colour(int(x)-dx,y);
                std::memcpy(pixels.data()+(size_t(y)*width+x)*4,c.data(),4);
            }
            return pixels;
        }
        const int left = occlusion ? int(width/3)+int(std::lround(shift*2)) : 0;
        for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
            const float source = float(x)-shift*(fractional ? .84375f : 1.0f);
            const int lo = int(std::floor(source));
            const float f = source-float(lo);
            const auto a = colour(lo,y), b = colour(lo+1,y);
            const size_t p = (size_t(y)*width+x)*4;
            if (diagonal) {
                const float sy = float(y)-shift*(fractional ? .453125f : .5f);
                const int iy = int(std::floor(sy)); const float fy = sy-float(iy);
                const auto aa=colour(lo,uint32_t(std::max(0,iy))),ab=colour(lo+1,uint32_t(std::max(0,iy)));
                const auto ba=colour(lo,uint32_t(std::max(0,iy+1))),bb=colour(lo+1,uint32_t(std::max(0,iy+1)));
                for (size_t c = 0; c < 4; ++c)
                    pixels[p+c] = uint8_t(std::lround((float(aa[c])*(1-f)+float(ab[c])*f)*(1-fy)+(float(ba[c])*(1-f)+float(bb[c])*f)*fy));
            } else {
                for (size_t c = 0; c < 4; ++c) pixels[p+c] = uint8_t(std::lround(float(a[c])*(1-f)+float(b[c])*f));
            }
            if (hud && x < width/3 && y < height/5) {
                const uint8_t value = x < 2 || y < 2 || x+2 >= width/3 || y+2 >= height/5 ? 16 : 235;
                pixels[p]=pixels[p+1]=pixels[p+2]=value; pixels[p+3]=255;
            }
            if (occlusion && int(x) >= left && int(x) < left+int(width/5) && y >= height/3 && y < height/2) {
                pixels[p]=0; pixels[p+1]=192; pixels[p+2]=255; pixels[p+3]=255;
            }
        }
        return pixels;
    };
    probe.factor = variant < 3 ? 2 : 2+(variant-3)%7;
    probe.still = variant >= 10 && variant < 17;
    probe.cut = variant >= 17;
    probe.a = picture(0); probe.b = picture(probe.still ? 0 : 16);
    if (probe.cut) for (size_t at = 0; at < bytes; at += 4) {
        probe.a[at] = probe.a[at+3] = 255; probe.a[at+1] = probe.a[at+2] = 0;
        probe.b[at+2] = probe.b[at+3] = 255; probe.b[at] = probe.b[at+1] = 0;
    }
    if (flat_new || flat_old || palette) for (size_t at = 0; at < bytes; at += 4) {
        if (flat_new) { probe.b[at]=0; probe.b[at+1]=255; probe.b[at+2]=0; probe.b[at+3]=255; }
        if (flat_old) { probe.a[at]=0; probe.a[at+1]=12; probe.a[at+2]=71; probe.a[at+3]=255; }
        if (palette) {
            const unsigned luma=(299u*probe.a[at]+587u*probe.a[at+1]+114u*probe.a[at+2])/1000u;
            const uint8_t colors[3][3]={{0,12,71},{0,79,39},{201,195,161}};
            const auto& color=colors[luma<85 ? 0 : luma<170 ? 1 : 2];
            for (unsigned c=0;c<3;++c) probe.b[at+c]=color[c];
            probe.b[at+3]=255;
        }
    }
    for (uint32_t i = 1; i < probe.factor; ++i) {
        if (probe.cut) probe.expected.push_back(i * 2 < probe.factor ? probe.a : probe.b);
        else probe.expected.push_back(picture(probe.still ? 0 : 16.0f*float(i)/float(probe.factor)));
    }
    const size_t output_bytes = bytes*(probe.factor-1);
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = output_bytes; bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    const auto device = vk_device();
    if (p_vkCreateBuffer(device,&bi,nullptr,&probe.buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements req{}; p_vkGetBufferMemoryRequirements(device,probe.buffer,&req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = readback_memory_type(req.memoryTypeBits);
    if (p_vkAllocateMemory(device,&ai,nullptr,&probe.memory) != VK_SUCCESS ||
        p_vkBindBufferMemory(device,probe.buffer,probe.memory,0) != VK_SUCCESS ||
        p_vkMapMemory(device,probe.memory,0,output_bytes,0,reinterpret_cast<void**>(&probe.mapped)) != VK_SUCCESS) return false;
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = 2;
    if (p_vkCreateQueryPool(device,&qi,nullptr,&probe.timing) == VK_SUCCESS) {
        p_vkCmdResetQueryPool(cb,probe.timing,0,2);
        p_vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,probe.timing,0);
    }
    state.history = false;
    set_framegen(true,int(probe.factor));
    FramegenFrames frames;
    for (const auto* pixels : {&probe.a,&probe.b}) {
        auto data = upload_alloc(bytes,16);
        std::memcpy(data.ptr,pixels->data(),bytes);
        transition(cb,probe.source,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy{}; copy.bufferOffset = data.offset;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent = {width,height,1};
        p_vkCmdCopyBufferToImage(cb,data.buffer,probe.source.image,probe.source.layout,1,&copy);
        if (!framegen_record(cb,probe.source,nullptr,frames)) return false;
    }
    if (frames.count != probe.factor) return false;
    for (uint32_t i = 0; i+1 < frames.count; ++i) {
        GpuImage* middle = frames.images[i];
        transition(cb,*middle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{}; copy.bufferOffset = bytes*i;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent = {width,height,1};
        p_vkCmdCopyImageToBuffer(cb,middle->image,middle->layout,probe.buffer,1,&copy);
    }
    if (std::getenv("NX_FRAMEGEN_PROBE_FLOW_DIGEST")) {
        for (size_t i=0;i<state.flow.size();++i)
            probe.flow_offsets[i+1]=probe.flow_offsets[i]+size_t(state.flow[i].width)*state.flow[i].height*4;
        bi.size=probe.flow_offsets.back();
        if (p_vkCreateBuffer(device,&bi,nullptr,&probe.flow_buffer) != VK_SUCCESS) return false;
        p_vkGetBufferMemoryRequirements(device,probe.flow_buffer,&req);
        ai.allocationSize=req.size;
        ai.memoryTypeIndex=readback_memory_type(req.memoryTypeBits);
        if (p_vkAllocateMemory(device,&ai,nullptr,&probe.flow_memory) != VK_SUCCESS ||
            p_vkBindBufferMemory(device,probe.flow_buffer,probe.flow_memory,0) != VK_SUCCESS ||
            p_vkMapMemory(device,probe.flow_memory,0,bi.size,0,reinterpret_cast<void**>(&probe.flow_mapped)) != VK_SUCCESS) return false;
        for (size_t i=0;i<state.flow.size();++i) {
            auto& field=state.flow[i]; transition(cb,field,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy copy{};copy.bufferOffset=probe.flow_offsets[i];
            copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={field.width,field.height,1};
            p_vkCmdCopyImageToBuffer(cb,field.image,field.layout,probe.flow_buffer,1,&copy);
        }
    }
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    p_vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
    if (probe.timing) p_vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,probe.timing,1);
    probe.recorded = true;
    return true;
}
bool framegen_probe_check() {
    bool ok = probe.recorded;
    if (ok) {
        for (size_t frame = 0; frame < probe.expected.size(); ++frame) {
            const auto& expected = probe.expected[frame];
            const uint8_t* actual = probe.mapped+frame*expected.size();
            double generated = 0, before = 0, after = 0;
            bool opaque = true;
            for (size_t i = 3; i < expected.size(); i += 4) opaque = opaque && actual[i] == 255;
            for (size_t i = 0; i < expected.size(); ++i) if (i%4 != 3) {
                generated += std::abs(int(actual[i])-int(expected[i]));
                before += std::abs(int(probe.a[i])-int(expected[i]));
                after += std::abs(int(probe.b[i])-int(expected[i]));
            }
            const double channels = double(expected.size()/4*3);
            const bool pass = opaque && (probe.compare_only || ((probe.still || probe.cut) ? generated==0 : generated < .9*std::min(before,after)));
            if (std::getenv("NX_FRAMEGEN_PROBE_DIGEST")) {
                uint64_t hash = 0xcbf29ce484222325ull;
                for (size_t i = 0; i < expected.size(); ++i) hash = (hash ^ actual[i]) * 0x100000001b3ull;
                log("framegen probe digest %ux %ux%u %s %zu: %016llx", probe.factor, probe.source.width,
                    probe.source.height, probe.cut ? "cut" : probe.still ? "still" : "moving", frame + 1,
                    (unsigned long long)hash);
            }
            ok = pass && ok;
            log("framegen probe %ux %ux%u %s %zu/%u: error %.3f, input errors %.3f / %.3f: %s",
                probe.factor,probe.source.width,probe.source.height,probe.cut ? "cut" : probe.still ? "still" : "moving",
                frame+1,probe.factor,generated/channels,before/channels,after/channels,pass ? (probe.compare_only ? "READBACK" : "PASS") : "FAIL");
        }
        if (probe.flow_mapped) for (size_t field=0;field<state.flow.size();++field) {
            uint64_t hash=0xcbf29ce484222325ull; bool finite=true;
            for (size_t b=probe.flow_offsets[field];b<probe.flow_offsets[field+1];++b)
                hash=(hash^probe.flow_mapped[b])*0x100000001b3ull;
            for (size_t b=probe.flow_offsets[field];b<probe.flow_offsets[field+1];b+=2) {
                uint16_t value;std::memcpy(&value,probe.flow_mapped+b,2);
                finite=finite && (value&0x7c00u)!=0x7c00u;
            }
            log("framegen probe flow digest %ux %ux%u %s %zu: %016llx",probe.factor,probe.source.width,probe.source.height,
                probe.cut ? "cut" : probe.still ? "still" : "moving",field,(unsigned long long)hash);
            log("framegen probe flow finite %zu: %s",field,finite ? "PASS" : "FAIL");
            ok=finite && ok;
        }
    } else log("framegen probe: missing interpolated images: FAIL");
    if (probe.recorded && probe.timing) {
        uint64_t ticks[2]{};
        if (p_vkGetQueryPoolResults(vk_device(),probe.timing,0,2,sizeof ticks,ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
            VkPhysicalDeviceProperties props{}; p_vkGetPhysicalDeviceProperties(vk_physical(),&props);
            log("framegen probe GPU workload: %.3f ms (two captures, %u intermediates)",double(ticks[1]-ticks[0])*props.limits.timestampPeriod/1e6,probe.factor-1);
        }
    }
    if (probe.timing) p_vkDestroyQueryPool(vk_device(),probe.timing,nullptr);
    if (probe.mapped) p_vkUnmapMemory(vk_device(),probe.memory);
    if (probe.buffer) p_vkDestroyBuffer(vk_device(),probe.buffer,nullptr);
    if (probe.memory) p_vkFreeMemory(vk_device(),probe.memory,nullptr);
    if (probe.flow_mapped) p_vkUnmapMemory(vk_device(),probe.flow_memory);
    if (probe.flow_buffer) p_vkDestroyBuffer(vk_device(),probe.flow_buffer,nullptr);
    if (probe.flow_memory) p_vkFreeMemory(vk_device(),probe.flow_memory,nullptr);
    free_image(probe.source); probe = {};
    return ok;
}
}
