// Asset-free Vulkan upload-memory diagnostic. Run only under an owned machine-wide
// game-test reservation; see docs/RENDERER-GPU-PROBES.md. No game files are needed.
// CPU operation timings include allocations/padding as printed below, exclude GPU
// submission/fence waits, and are not FPS measurements. Disable validation when
// collecting timings; enable it for a separate correctness run.
// Snapshot batch: 8 MiB payload + 65,552 zero bytes per range, ABBAABBA.
// Scratch batch: 64 MiB payload, no extra padding, ABBAABBA.
#include <vulkan/vulkan.h>
#include "upload_snapshot.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        VkResult r = (x);                                                                                    \
        if (r != VK_SUCCESS) {                                                                               \
            std::fprintf(stderr, "Vulkan failure %d at %d\n", int(r), __LINE__);                             \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (0)
using Snapshot = std::shared_ptr<std::vector<uint8_t>>;
__attribute__((noinline)) Snapshot old_snapshot(uint8_t* mapped, const uint8_t* guest, size_t size,
                                                size_t padded) {
    std::memcpy(mapped, guest, size);
    std::memset(mapped + size, 0, padded - size);
    return std::make_shared<std::vector<uint8_t>>(mapped, mapped + size);
}
__attribute__((noinline)) Snapshot new_snapshot(uint8_t* mapped, const uint8_t* guest, size_t size,
                                                size_t padded) {
    return nx::nvn::snapshot_upload(mapped, guest, size, padded);
}
__attribute__((noinline)) void old_initialize(uint8_t* mapped, const uint8_t* source, size_t size,
                                              size_t padded) {
    std::memset(mapped, 0, padded);
    if (source && size)
        std::memcpy(mapped, source, size);
}
__attribute__((noinline)) void new_initialize(uint8_t* mapped, const uint8_t* source, size_t size,
                                              size_t padded) {
    nx::nvn::initialize_upload(mapped, source, size, padded);
}
int main(int argc, char** argv) {
    if (argc != 1 && (argc != 3 || std::strcmp(argv[1], "--gpu") != 0)) {
        std::fprintf(stderr, "Usage: upload_memory_bench [--gpu INDEX]\n");
        return 2;
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance instance;
    CHECK(vkCreateInstance(&ici, nullptr, &instance));
    uint32_t count = 0;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    if (!count)
        return 77;
    std::vector<VkPhysicalDevice> devices(count);
    CHECK(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    uint32_t selected = 0;
    bool found_discrete = false;
    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(devices[i], &p);
        std::printf("device %u: %s\n", i, p.deviceName);
        if (!found_discrete && p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            selected = i;
            found_discrete = true;
        }
    }
    if (argc == 3) {
        char* end = nullptr;
        const unsigned long index = std::strtoul(argv[2], &end, 10);
        if (!argv[2][0] || *end || index >= count) {
            std::fprintf(stderr, "GPU index is outside the enumerated device list\n");
            vkDestroyInstance(instance, nullptr);
            return 2;
        }
        selected = uint32_t(index);
    }
    VkPhysicalDevice gpu = devices[selected];
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(gpu, &props);
    std::printf("device: %s\n", props.deviceName);
    uint32_t qcount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qcount, nullptr);
    std::vector<VkQueueFamilyProperties> qs(qcount);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qcount, qs.data());
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < qcount; ++i)
        if (qs[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
            family = i;
            break;
        }
    if (family == UINT32_MAX)
        return 77;
    float priority = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    VkDevice device;
    CHECK(vkCreateDevice(gpu, &dci, nullptr, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    constexpr size_t size = 1u << 20;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = 2 * size + 65552;
    // Match the upload ring's usage so memoryTypeBits reflect the same resource kind.
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    VkBuffer buffer;
    CHECK(vkCreateBuffer(device, &bi, nullptr, &buffer));
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, buffer, &req);
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &mp);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((req.memoryTypeBits >> i & 1) &&
            (mp.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            type = i;
            break;
        }
    if (type == UINT32_MAX)
        return 77;
    std::printf("mapped source memory: type=%u flags=0x%x (same first coherent type as the upload ring)\n",
                type, mp.memoryTypes[type].propertyFlags);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    VkDeviceMemory memory;
    CHECK(vkAllocateMemory(device, &ai, nullptr, &memory));
    CHECK(vkBindBufferMemory(device, buffer, memory, 0));
    void* mapped = nullptr;
    CHECK(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped));

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = family;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(device, &pci, nullptr, &pool));
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb;
    CHECK(vkAllocateCommandBuffers(device, &cai, &cb));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    CHECK(vkCreateFence(device, &fci, nullptr, &fence));
    std::vector<uint8_t> guest(size);
    for (size_t i = 0; i < size; ++i)
        guest[i] = uint8_t((i * 73 + (i >> 9)) & 255);
    for (auto fn : {old_snapshot, new_snapshot}) {
        auto before = fn(static_cast<uint8_t*>(mapped), guest.data(), size, size + 65552);
        if (*before != guest) {
            std::puts("snapshot correctness: FAIL");
            return 1;
        }
        CHECK(vkResetCommandBuffer(cb, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CHECK(vkBeginCommandBuffer(cb, &begin));
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &host, 0,
                             nullptr, 0, nullptr);
        VkBufferCopy copy{0, size + 65552, size};
        vkCmdCopyBuffer(cb, buffer, buffer, 1, &copy);
        VkMemoryBarrier read{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        read.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &read, 0,
                             nullptr, 0, nullptr);
        CHECK(vkEndCommandBuffer(cb));
        CHECK(vkResetFences(device, 1, &fence));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        CHECK(vkQueueSubmit(queue, 1, &si, fence));
        CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5'000'000'000ull));
        if (std::memcmp(static_cast<uint8_t*>(mapped) + size + 65552, guest.data(), size)) {
            std::puts("GPU upload correctness: FAIL");
            return 1;
        }
        std::printf("%s snapshot and GPU copy correctness: PASS\n", fn == old_snapshot ? "old" : "new");
    }
    for (size_t bytes : {size_t(256), size_t(4096), size_t(65536), size}) {
        unsigned iterations = unsigned(std::clamp<size_t>((8u << 20) / bytes, 8, 32768));
        for (unsigned sample = 0; sample < 8; ++sample) {
            bool old = "abbaabba"[sample] == 'a';
            auto fn = old ? old_snapshot : new_snapshot;
            uint64_t checksum = 0;
            auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i) {
                auto result = fn(static_cast<uint8_t*>(mapped), guest.data(), bytes, bytes + 65552);
                checksum += (*result)[(i * 37) % bytes];
            }
            double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::printf("{\"algorithm\":\"%s\",\"bytes\":%zu,\"sample\":%u,\"iterations\":%u,\"ms\":%.6f,"
                        "\"checksum\":%llu}\n",
                        old ? "old" : "new", bytes, sample, iterations, ms, (unsigned long long)checksum);
        }
    }
    for (size_t bytes : {size_t(256), size_t(4096), size_t(65536), size}) {
        unsigned iterations = unsigned((64u << 20) / bytes);
        for (unsigned sample = 0; sample < 8; ++sample) {
            bool old = "abbaabba"[sample] == 'a';
            auto fn = old ? old_initialize : new_initialize;
            auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i) {
                fn(static_cast<uint8_t*>(mapped), guest.data(), bytes, bytes);
                asm volatile("" : : "g"(mapped) : "memory");
            }
            double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (std::memcmp(mapped, guest.data(), bytes)) {
                std::puts("scratch contents: FAIL");
                return 1;
            }
            std::printf("{\"kind\":\"scratch\",\"algorithm\":\"%s\",\"bytes\":%zu,\"sample\":%u,"
                        "\"iterations\":%u,\"ms\":%.6f}\n",
                        old ? "old" : "new", bytes, sample, iterations, ms);
        }
    }
    CHECK(vkDeviceWaitIdle(device));
    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
    vkUnmapMemory(device, memory);
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
}
