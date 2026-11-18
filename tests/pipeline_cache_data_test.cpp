#include "pipeline_cache_data.h"
#include <cstdio>
#include <cstdlib>

void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
    using namespace nx::nvn;
    VkPhysicalDeviceProperties gpu{};
    gpu.vendorID = 0x1002; gpu.deviceID = 0x67df; gpu.pipelineCacheUUID[0] = 42;
    VkPipelineCacheHeaderVersionOne header{sizeof header, VK_PIPELINE_CACHE_HEADER_VERSION_ONE, gpu.vendorID, gpu.deviceID, {42}};
    check(compatible_pipeline_cache_header(&header, sizeof header, gpu), "same GPU/driver cache accepted");
    check(!compatible_pipeline_cache_header(&header, sizeof header - 1, gpu), "truncated header rejected");
    check(!compatible_pipeline_cache_header(&header, kMaxPipelineCacheBytes + 1, gpu), "oversized file rejected before allocation");
    auto wrong = header; wrong.deviceID++;
    check(!compatible_pipeline_cache_header(&wrong, sizeof wrong, gpu), "different GPU rejected");
    wrong = header; wrong.pipelineCacheUUID[0]++;
    check(!compatible_pipeline_cache_header(&wrong, sizeof wrong, gpu), "different driver UUID rejected");
    wrong = header; wrong.headerSize = sizeof wrong + 8;
    check(!compatible_pipeline_cache_header(&wrong, sizeof wrong, gpu), "header outside file rejected");
    wrong = header; wrong.headerVersion = VkPipelineCacheHeaderVersion(99);
    check(!compatible_pipeline_cache_header(&wrong, sizeof wrong, gpu), "unknown header version rejected");
    std::vector<uint8_t> data;
    int reads = 0;
    check(pipeline_cache_snapshot([&](size_t* size, void* bytes) {
        if (!bytes) { *size = 64; return VK_SUCCESS; }
        if (++reads == 1) return VK_INCOMPLETE;
        check(*size == 64, "growing cache size is queried again");
        std::memset(bytes, 7, 60); *size = 60; return VK_SUCCESS;
    }, data, 32) && data.size() == 60 && data.back() == 7, "growing/shrinking cache yields a complete snapshot");
    reads = 0;
    check(!pipeline_cache_snapshot([&](size_t* size, void* bytes) {
        if (!bytes) { *size = 64; return VK_SUCCESS; }
        ++reads; return VK_INCOMPLETE;
    }, data, 32) && reads == 3 && data.empty(), "continuous growth is bounded and incomplete bytes discarded");
    check(!pipeline_cache_snapshot([](size_t*, void*) { return VK_ERROR_DEVICE_LOST; }, data, 32) && data.empty(), "driver error cannot publish a snapshot");
    check(!pipeline_cache_snapshot([](size_t*, void*) { std::abort(); return VK_SUCCESS; }, data, kMaxPipelineCacheBytes + 1), "oversized snapshots never call the driver");
    std::puts("pipeline cache header and concurrent-growth snapshot tests passed");
}
