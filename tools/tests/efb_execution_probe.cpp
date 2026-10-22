// Real Vulkan execution of the production EFB dispatch, with synthetic pixels.
#include "gx/vk/vk_common.h"
#include <cassert>
#include <fstream>
#include <iostream>

static void check(VkResult result) { assert(result == VK_SUCCESS); }
static std::vector<char> readFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
extern "C" int __android_log_print(int, const char*, const char*, ...) { return 0; }
namespace gpu { void VkRenderer::shutdown() {} }

int main(int argc, char** argv) {
    assert(argc == 7);
    const uint32_t ctrl = strtoul(argv[2], nullptr, 0), pixelFormat = strtoul(argv[3], nullptr, 0);
    const bool half = (ctrl & 512) != 0;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    const char* validation = "VK_LAYER_KHRONOS_validation";
    if (getenv("GALAXY_REQUIRE_VK_VALIDATION")) {
        uint32_t layersCount = 0;
        check(vkEnumerateInstanceLayerProperties(&layersCount, nullptr));
        std::vector<VkLayerProperties> layers(layersCount);
        check(vkEnumerateInstanceLayerProperties(&layersCount, layers.data()));
        bool found = false;
        for (const auto& layer : layers) if (!strcmp(layer.layerName, validation)) found = true;
        assert(found);
        instanceInfo.enabledLayerCount = 1;
        instanceInfo.ppEnabledLayerNames = &validation;
    }
    VkInstance instance;
    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) return 77;
    if (getenv("GALAXY_REQUIRE_VK_VALIDATION")) std::cerr << "Validation layer active\n";
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    if (!count) return 77;
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    gpu::VkRenderer r;
    r.mPhysicalDevice = devices[0];
    vkGetPhysicalDeviceQueueFamilyProperties(r.mPhysicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count);
    vkGetPhysicalDeviceQueueFamilyProperties(r.mPhysicalDevice, &count, queues.data());
    uint32_t family = 0;
    const auto required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    while (family < count && (queues[family].queueFlags & required) != required) family++;
    if (family == count) return 77;
    float priority = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    check(vkCreateDevice(r.mPhysicalDevice, &dci, nullptr, &r.mDevice));
    vkGetDeviceQueue(r.mDevice, family, 0, &r.mQueue);
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(r.mPhysicalDevice, &props);
    auto memory = [&](VkMemoryRequirements req, VkMemoryPropertyFlags flags) {
        uint32_t type = 0;
        while (type < props.memoryTypeCount && (!(req.memoryTypeBits & (1u << type)) ||
               (props.memoryTypes[type].propertyFlags & flags) != flags)) type++;
        assert(type < props.memoryTypeCount);
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        info.allocationSize = req.size; info.memoryTypeIndex = type;
        VkDeviceMemory result;
        check(vkAllocateMemory(r.mDevice, &info, nullptr, &result));
        return result;
    };
    gpu::VkTarget target{}; target.width = target.height = 4;
    gpu::VkCopyTex dst{}; dst.w = dst.h = half ? 2 : 4;
    VkImage images[3]; VkImageView views[3]; VkDeviceMemory imageMem[3];
    for (int i = 0; i < 3; i++) {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = {uint32_t(i == 2 ? dst.w : 4), uint32_t(i == 2 ? dst.h : 4), 1};
        info.mipLevels = info.arrayLayers = 1;
        info.format = i == 1 ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            (i == 2 ? VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT :
             i == 1 ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        check(vkCreateImage(r.mDevice, &info, nullptr, &images[i]));
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(r.mDevice, images[i], &req);
        imageMem[i] = memory(req, 0);
        check(vkBindImageMemory(r.mDevice, images[i], imageMem[i], 0));
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = images[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = info.format;
        view.subresourceRange = {i == 1 ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(r.mDevice, &view, nullptr, &views[i]));
    }
    target.colorImage = images[0]; target.colorView = views[0];
    target.depthImage = images[1]; target.depthSampleView = views[1];
    target.colorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    r.mDepthFormat = VK_FORMAT_D32_SFLOAT;
    dst.image = images[2]; dst.view = views[2];
    auto input = readFile(argv[4]), depths = readFile(argv[5]);
    assert(input.size() == 64 && depths.size() == 64);
    input.insert(input.end(), depths.begin(), depths.end());
    VkBuffer buffer;
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = 192; bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(vkCreateBuffer(r.mDevice, &bufferInfo, nullptr, &buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(r.mDevice, buffer, &req);
    VkDeviceMemory bufferMem = memory(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkBindBufferMemory(r.mDevice, buffer, bufferMem, 0));
    void* mapped;
    check(vkMapMemory(r.mDevice, bufferMem, 0, 192, 0, &mapped));
    memcpy(mapped, input.data(), input.size());
    VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 3; li.pBindings = bindings;
    check(vkCreateDescriptorSetLayout(r.mDevice, &li, nullptr, &r.mCopyDescLayout));
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 32};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1; pli.pSetLayouts = &r.mCopyDescLayout;
    pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(r.mDevice, &pli, nullptr, &r.mCopyPipelineLayout));
    auto code = readFile(argv[1]);
    VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mi.codeSize = code.size(); mi.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module;
    check(vkCreateShaderModule(r.mDevice, &mi, nullptr, &module));
    VkComputePipelineCreateInfo pi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pi.layout = r.mCopyPipelineLayout;
    pi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; pi.stage.module = module; pi.stage.pName = "main";
    check(vkCreateComputePipelines(r.mDevice, VK_NULL_HANDLE, 1, &pi, nullptr, &r.mCopyPipeline));
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    check(vkCreateSampler(r.mDevice, &si, nullptr, &r.mCopySampler));
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = family;
    VkCommandPool commandPool;
    check(vkCreateCommandPool(r.mDevice, &poolInfo, nullptr, &commandPool));
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = commandPool; ai.commandBufferCount = 1; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    VkCommandBuffer cmd;
    check(vkAllocateCommandBuffers(r.mDevice, &ai, &cmd));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(cmd, &begin));
    for (int i = 0; i < 2; i++) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.image = images[i]; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange = {i ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy region{};
        region.bufferOffset = i * 64; region.imageSubresource = {barrier.subresourceRange.aspectMask, 0, 0, 1};
        region.imageExtent = {4, 4, 1};
        vkCmdCopyBufferToImage(cmd, buffer, images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = i ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : target.colorLayout;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | (i ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0);
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);
    }
    assert(r.copyEfbTexture(cmd, target, dst, 0, 0, 4, 4, ctrl, pixelFormat));
    VkImageMemoryBarrier out{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    out.image = dst.image; out.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    out.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    out.srcQueueFamilyIndex = out.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    out.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    out.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; out.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &out);
    VkBufferImageCopy readback{};
    readback.bufferOffset = 128; readback.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    readback.imageExtent = {uint32_t(dst.w), uint32_t(dst.h), 1};
    vkCmdCopyImageToBuffer(cmd, dst.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &readback);
    check(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
    check(vkQueueSubmit(r.mQueue, 1, &submit, VK_NULL_HANDLE)); check(vkQueueWaitIdle(r.mQueue));
    std::ofstream(argv[6], std::ios::binary).write(static_cast<const char*>(mapped) + 128, dst.w * dst.h * 4);
    r.releaseRetiredImageSets();
    vkDestroyCommandPool(r.mDevice, commandPool, nullptr);
    for (auto pool : r.mSamplerPools) vkDestroyDescriptorPool(r.mDevice, pool, nullptr);
    vkDestroyPipeline(r.mDevice, r.mCopyPipeline, nullptr);
    vkDestroyShaderModule(r.mDevice, module, nullptr);
    vkDestroySampler(r.mDevice, r.mCopySampler, nullptr);
    vkDestroyPipelineLayout(r.mDevice, r.mCopyPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(r.mDevice, r.mCopyDescLayout, nullptr);
    for (int i = 0; i < 3; i++) {
        vkDestroyImageView(r.mDevice, views[i], nullptr); vkDestroyImage(r.mDevice, images[i], nullptr);
        vkFreeMemory(r.mDevice, imageMem[i], nullptr);
    }
    vkUnmapMemory(r.mDevice, bufferMem); vkDestroyBuffer(r.mDevice, buffer, nullptr); vkFreeMemory(r.mDevice, bufferMem, nullptr);
    vkDestroyDevice(r.mDevice, nullptr); vkDestroyInstance(instance, nullptr);
}
