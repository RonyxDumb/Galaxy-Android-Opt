// Execute the generated GLSL bodies on a real Vulkan implementation, including
// software Vulkan in CI. This tests numerical results, not source spellings.
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void check(VkResult r) {
    if (r != VK_SUCCESS) { fprintf(stderr, "Vulkan result %d\n", r); std::exit(2); }
}

static std::vector<char> readFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) std::exit(3);
    fseek(f, 0, SEEK_END);
    std::vector<char> data((size_t)ftell(f));
    rewind(f);
    if (fread(data.data(), 1, data.size(), f) != data.size()) std::exit(4);
    fclose(f);
    return data;
}

int main(int argc, char** argv) {
    if (argc != 6) return 1;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance instance;
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) return 77;
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    if (!count) { vkDestroyInstance(instance, nullptr); return 77; }
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    VkPhysicalDevice physical = devices.front();
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues.data());
    uint32_t family = 0;
    while (family < count && !(queues[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) family++;
    if (family == count) return 77;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    VkDevice device;
    check(vkCreateDevice(physical, &dci, nullptr, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physical, &props);

    std::vector<char> data[6] = {readFile(argv[2]), readFile(argv[3]), readFile(argv[4]),
                               std::vector<char>(4), std::vector<char>(256), std::vector<char>(256)};
    VkBuffer buffers[6]{};
    VkDeviceMemory memory[6]{};
    for (int i = 0; i < 6; i++) {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = data[i].size(); bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bci.usage = i == 4 ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        check(vkCreateBuffer(device, &bci, nullptr, &buffers[i]));
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(device, buffers[i], &req);
        uint32_t type = 0;
        VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        while (type < props.memoryTypeCount && (!(req.memoryTypeBits & (1u << type)) ||
               (props.memoryTypes[type].propertyFlags & wanted) != wanted)) type++;
        if (type == props.memoryTypeCount) return 77;
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = req.size; mai.memoryTypeIndex = type;
        check(vkAllocateMemory(device, &mai, nullptr, &memory[i]));
        check(vkBindBufferMemory(device, buffers[i], memory[i], 0));
        void* mapped;
        check(vkMapMemory(device, memory[i], 0, data[i].size(), 0, &mapped));
        memcpy(mapped, data[i].data(), data[i].size());
        vkUnmapMemory(device, memory[i]);
    }
    VkDescriptorSetLayoutBinding bindings[5]{};
    for (int i = 0; i < 5; i++) {
        bindings[i].binding = i; bindings[i].descriptorCount = 1;
        bindings[i].descriptorType = i == 4 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayout layouts[3];
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = 5; lci.pBindings = bindings;
    check(vkCreateDescriptorSetLayout(device, &lci, nullptr, &layouts[0]));
    lci.bindingCount = 0;
    check(vkCreateDescriptorSetLayout(device, &lci, nullptr, &layouts[1]));
    lci.bindingCount = 1;
    check(vkCreateDescriptorSetLayout(device, &lci, nullptr, &layouts[2]));
    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 3; dpci.poolSizeCount = 2; dpci.pPoolSizes = sizes;
    VkDescriptorPool pool;
    check(vkCreateDescriptorPool(device, &dpci, nullptr, &pool));
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = pool; dsai.descriptorSetCount = 3; dsai.pSetLayouts = layouts;
    VkDescriptorSet sets[3];
    check(vkAllocateDescriptorSets(device, &dsai, sets));
    VkDescriptorBufferInfo dbi[6]{};
    VkWriteDescriptorSet writes[6]{};
    for (int i = 0; i < 6; i++) {
        dbi[i] = {buffers[i], 0, data[i].size()};
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = i < 5 ? sets[0] : sets[2]; writes[i].dstBinding = i < 5 ? i : 0;
        writes[i].descriptorCount = 1; writes[i].descriptorType = bindings[i < 5 ? i : 0].descriptorType;
        writes[i].pBufferInfo = &dbi[i];
    }
    vkUpdateDescriptorSets(device, 6, writes, 0, nullptr);
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 3; plci.pSetLayouts = layouts; plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &push;
    VkPipelineLayout layout;
    check(vkCreatePipelineLayout(device, &plci, nullptr, &layout));
    auto code = readFile(argv[1]);
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = code.size(); smci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module;
    check(vkCreateShaderModule(device, &smci, nullptr, &module));
    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; cpci.stage.module = module; cpci.stage.pName = "main";
    cpci.layout = layout;
    VkPipeline pipeline;
    check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline));
    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.queueFamilyIndex = family;
    VkCommandPool cmdPool;
    check(vkCreateCommandPool(device, &cpi, nullptr, &cmdPool));
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = cmdPool; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    check(vkAllocateCommandBuffers(device, &cbai, &cmd));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(cmd, &begin));
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 3, sets, 0, nullptr);
    uint32_t ordinal = 0;
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &ordinal);
    vkCmdDispatch(cmd, 1, 1, 1);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    check(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    check(vkCreateFence(device, &fci, nullptr, &fence));
    check(vkQueueSubmit(queue, 1, &submit, fence));
    check(vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ull));
    void* mapped;
    check(vkMapMemory(device, memory[5], 0, data[5].size(), 0, &mapped));
    FILE* out = fopen(argv[5], "wb");
    if (!out || fwrite(mapped, 1, data[5].size(), out) != data[5].size()) return 6;
    fclose(out);
    vkUnmapMemory(device, memory[5]);
    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, cmdPool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, module, nullptr);
    vkDestroyPipelineLayout(device, layout, nullptr);
    vkDestroyDescriptorPool(device, pool, nullptr);
    for (auto l : layouts) vkDestroyDescriptorSetLayout(device, l, nullptr);
    for (int i = 0; i < 6; i++) { vkDestroyBuffer(device, buffers[i], nullptr); vkFreeMemory(device, memory[i], nullptr); }
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}
