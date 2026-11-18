#include "pipeline_cache_data.h"
#include "pipeline_cache_progress.h"
#include <cstdio>
#include <cstdlib>

void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
    using namespace nx::nvn;
    PipelineCacheProgress progress;
    check(!progress.dirty() && !progress.periodic_due(), "loaded cache needs no export");
    progress.changed();
    const auto first = progress.revision();
    check(progress.periodic_due(), "new pipelines need a periodic inspection");
    progress.not_grown(first);
    for (int poll = 0; poll < 1000; ++poll)
        check(!progress.periodic_due() && progress.dirty(), "same-size cache skips unchanged polls but still owes shutdown save");
    progress.failed();
    check(progress.periodic_due(), "failed forced export remains retryable");
    progress.not_grown(first);
    progress.changed();
    check(progress.periodic_due(), "same-size observation cannot hide later mutations");
    const auto second = progress.revision();
    progress.changed();  // a compiler worker finishes while the driver snapshot is being read
    progress.saved(second);
    check(progress.dirty() && progress.periodic_due(), "concurrent mutation survives successful save");
    const auto third = progress.revision();
    progress.not_grown(third);
    check(progress.dirty() && !progress.periodic_due(), "new no-growth observation defers only periodic polling");
    progress.saved(third);
    check(!progress.dirty() && !progress.periodic_due(), "successful forced save settles current revision");
    progress.changed();
    progress.failed();
    check(progress.periodic_due(), "failed size/snapshot/file export retries without another mutation");
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
