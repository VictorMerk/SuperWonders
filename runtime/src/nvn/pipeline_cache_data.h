#pragma once
#include <cstring>
#include <new>
#include <vector>
#include <vulkan/vulkan.h>

namespace nx::nvn {
inline constexpr size_t kMaxPipelineCacheBytes = 512ull << 20;

// Inspect the small header before allocating for or passing a disk file to the driver.
inline bool compatible_pipeline_cache_header(const void* data, size_t bytes, const VkPhysicalDeviceProperties& gpu) {
    if (!data || bytes < sizeof(VkPipelineCacheHeaderVersionOne) || bytes > kMaxPipelineCacheBytes) return false;
    VkPipelineCacheHeaderVersionOne header{};
    std::memcpy(&header, data, sizeof header);
    return header.headerSize >= sizeof header && header.headerSize <= bytes &&
        header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
        header.vendorID == gpu.vendorID && header.deviceID == gpu.deviceID &&
        std::memcmp(header.pipelineCacheUUID, gpu.pipelineCacheUUID, VK_UUID_SIZE) == 0;
}

// A compile can grow the driver's cache between the size query and copy. Retry a
// bounded number of times; an incomplete snapshot must never replace a warm file.
template<class GetData>
bool pipeline_cache_snapshot(GetData get, std::vector<uint8_t>& output, size_t first_size) {
    output.clear();
    size_t size = first_size;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!size || size > kMaxPipelineCacheBytes) { output.clear(); return false; }
        try { output.resize(size); } catch (const std::bad_alloc&) { output.clear(); return false; }
        size_t written = size;
        const VkResult result = get(&written, output.data());
        if (result == VK_SUCCESS && written > 0 && written <= size) {
            output.resize(written);
            return true;
        }
        if (result != VK_INCOMPLETE) break;
        if (get(&size, nullptr) != VK_SUCCESS) break;
    }
    output.clear();
    return false;
}
}
