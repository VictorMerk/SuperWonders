// Native GPU regression for logical 32-lane ballots/shuffles. No game data.
#include <vulkan/vulkan.h>
#include "nvn_spirv_warp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
    std::fprintf(stderr, "%s failed: %d\n", #call, int(r)); std::exit(1); } } while (0)

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 20) return 1;
    const size_t bytes = size_t(file.tellg());
    if (bytes % 4) return 1;
    std::vector<uint32_t> original(bytes / 4), fixed;
    file.seekg(0); file.read(reinterpret_cast<char*>(original.data()), bytes);
    if (!nx::nvn::patch_warp32(original, fixed)) return 1;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &app;
    VkInstance instance; CHECK(vkCreateInstance(&ici, nullptr, &instance));
    uint32_t count = 0; CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    if (!count) { std::puts("no GPU"); vkDestroyInstance(instance, nullptr); return 77; }
    std::vector<VkPhysicalDevice> devices(count); CHECK(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    VkPhysicalDevice gpu = devices[0];
    VkPhysicalDeviceSubgroupProperties sub{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2}; props.pNext = &sub;
    vkGetPhysicalDeviceProperties2(gpu, &props);
    std::printf("GPU: %s, subgroup size %u\n", props.properties.deviceName, sub.subgroupSize);
    if (sub.subgroupSize < 32 || !(sub.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) ||
        (sub.supportedOperations & (VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_BIT)) !=
        (VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_BIT)) {
        vkDestroyInstance(instance, nullptr); return 77;
    }
    uint32_t families = 0, family = UINT32_MAX; vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, nullptr);
    std::vector<VkQueueFamilyProperties> queues(families); vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, queues.data());
    for (uint32_t i = 0; i < families; ++i) if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { family = i; break; }
    if (family == UINT32_MAX) { vkDestroyInstance(instance, nullptr); return 77; }
    const float priority = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    VkDevice device; CHECK(vkCreateDevice(gpu, &dci, nullptr, &device));
    VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue);
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo slci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    slci.bindingCount = 1; slci.pBindings = &binding;
    VkDescriptorSetLayout set_layout; CHECK(vkCreateDescriptorSetLayout(device, &slci, nullptr, &set_layout));
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1; plci.pSetLayouts = &set_layout;
    VkPipelineLayout layout; CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &layout));
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bci.size = 128 * 4 * sizeof(uint32_t);
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkBuffer buffer; CHECK(vkCreateBuffer(device, &bci, nullptr, &buffer));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device, buffer, &req);
    VkPhysicalDeviceMemoryProperties mem; vkGetPhysicalDeviceMemoryProperties(gpu, &mem);
    uint32_t memory_type = UINT32_MAX;
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) && (mem.memoryTypes[i].propertyFlags &
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { memory_type = i; break; }
    if (memory_type == UINT32_MAX) {
        vkDestroyBuffer(device, buffer, nullptr); vkDestroyPipelineLayout(device, layout, nullptr);
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr); vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr); return 77;
    }
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = req.size; mai.memoryTypeIndex = memory_type;
    VkDeviceMemory memory; CHECK(vkAllocateMemory(device, &mai, nullptr, &memory));
    CHECK(vkBindBufferMemory(device, buffer, memory, 0));
    void* mapped; CHECK(vkMapMemory(device, memory, 0, req.size, 0, &mapped));
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = &pool_size;
    VkDescriptorPool pool; CHECK(vkCreateDescriptorPool(device, &dpci, nullptr, &pool));
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = pool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &set_layout;
    VkDescriptorSet set; CHECK(vkAllocateDescriptorSets(device, &dsai, &set));
    VkDescriptorBufferInfo dbi{buffer, 0, bci.size};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; write.dstSet = set;
    write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.queueFamilyIndex = family; cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool command_pool; CHECK(vkCreateCommandPool(device, &cpci, nullptr, &command_pool));
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = command_pool; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
    VkCommandBuffer command; CHECK(vkAllocateCommandBuffers(device, &cbai, &command));
    auto run = [&](const std::vector<uint32_t>& code) {
        std::memset(mapped, 0xcd, bci.size);
        VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; smci.codeSize = code.size() * 4; smci.pCode = code.data();
        VkShaderModule module; CHECK(vkCreateShaderModule(device, &smci, nullptr, &module));
        VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; pci.layout = layout;
        pci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        pci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; pci.stage.module = module; pci.stage.pName = "main";
        VkPipeline pipeline; CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline));
        CHECK(vkResetCommandBuffer(command, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CHECK(vkBeginCommandBuffer(command, &begin));
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
        vkCmdDispatch(command, 1, 1, 1);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        CHECK(vkEndCommandBuffer(command));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE)); CHECK(vkQueueWaitIdle(queue));
        unsigned bad = 0; auto* results = static_cast<uint32_t*>(mapped);
        for (unsigned i = 0; i < 128; ++i) {
            const auto* r = results + 4 * i;
            const uint32_t lane = i & 31u;
            const bool mismatch = r[0] != (i & ~31u) || r[1] != (1u << ((i >> 5u) + 1u)) ||
                                  r[2] != lane || r[3] != ((1u << lane) - 1u);
            if (mismatch && bad < 4) std::printf("lane %u: shuffle %u, ballot %#x, lane-id %u, lt-mask %#x\n",
                                               i, r[0], r[1], r[2], r[3]);
            bad += mismatch;
        }
        vkDestroyPipeline(device, pipeline, nullptr); vkDestroyShaderModule(device, module, nullptr);
        return bad;
    };
    const unsigned before = run(original), after = run(fixed);
    std::printf("Incorrect invocations: before %u/128, after %u/128\n", before, after);
    vkDestroyCommandPool(device, command_pool, nullptr); vkDestroyDescriptorPool(device, pool, nullptr);
    vkUnmapMemory(device, memory); vkDestroyBuffer(device, buffer, nullptr); vkFreeMemory(device, memory, nullptr);
    vkDestroyPipelineLayout(device, layout, nullptr); vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
    vkDestroyDevice(device, nullptr); vkDestroyInstance(instance, nullptr);
    return after || (sub.subgroupSize > 32 && !before) ? 1 : 0;
}
