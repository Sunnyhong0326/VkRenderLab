#include "VulkanContext.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr const char* validationLayer = "VK_LAYER_KHRONOS_validation";

VKAPI_ATTR VkBool32 VKAPI_CALL onValidationMessage(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* message, void* user)
{
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) &&
        (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT))
        ++*static_cast<std::atomic_uint*>(user);
    std::cerr << message->pMessage << '\n';
    return VK_FALSE;
}

bool hasLayer(const char* name)
{
    uint32_t count = 0;
    checkVk(vkEnumerateInstanceLayerProperties(&count, nullptr), "Enumerate instance layers");
    std::vector<VkLayerProperties> layers(count);
    checkVk(vkEnumerateInstanceLayerProperties(&count, layers.data()), "Read instance layers");
    return std::ranges::any_of(layers, [name](const auto& layer) {
        return std::strcmp(layer.layerName, name) == 0;
    });
}

bool supportsSwapchain(VkPhysicalDevice device)
{
    uint32_t count = 0;
    checkVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
            "Enumerate device extensions");
    std::vector<VkExtensionProperties> extensions(count);
    checkVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()),
            "Read device extensions");
    return std::ranges::any_of(extensions, [](const auto& extension) {
        return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
    });
}

bool supportsFormat(VkPhysicalDevice device, VkFormat format, VkFormatFeatureFlags required)
{
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(device, format, &properties);
    return (properties.optimalTilingFeatures & required) == required;
}
}

void checkVk(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(result) + ").");
}

Buffer::~Buffer()
{
    if (handle)
        vkDestroyBuffer(device, handle, nullptr);
    if (memory)
        vkFreeMemory(device, memory, nullptr);
}

Image::~Image()
{
    if (view)
        vkDestroyImageView(device, view, nullptr);
    if (handle)
        vkDestroyImage(device, handle, nullptr);
    if (memory)
        vkFreeMemory(device, memory, nullptr);
}

VulkanContext::~VulkanContext()
{
    if (commandPool)
        vkDestroyCommandPool(device, commandPool, nullptr);
    if (device)
        vkDestroyDevice(device, nullptr);
    if (surface)
        vkDestroySurfaceKHR(instance, surface, nullptr);
    if (debugMessenger)
    {
        const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        destroy(instance, debugMessenger, nullptr);
    }
    if (instance)
        vkDestroyInstance(instance, nullptr);
}

void VulkanContext::initialize(GLFWwindow* window, bool validate)
{
    const VkApplicationInfo application{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "VkRenderLab",
        .apiVersion = VK_API_VERSION_1_3,
    };
    uint32_t extensionCount = 0;
    const auto* required = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (!required)
        throw std::runtime_error("GLFW could not find Vulkan surface extensions.");
    std::vector<const char*> extensions(required, required + extensionCount);

    VkDebugUtilsMessengerCreateInfoEXT debugInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = onValidationMessage,
        .pUserData = &validationErrors,
    };
    const VkValidationFeatureEnableEXT sync = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    const VkValidationFeaturesEXT validation{
        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pNext = &debugInfo,
        .enabledValidationFeatureCount = 1,
        .pEnabledValidationFeatures = &sync,
    };
    if (validate)
    {
        if (!hasLayer(validationLayer))
            throw std::runtime_error("Validation requested. Install the Vulkan SDK validation layer.");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    }
    const VkInstanceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = validate ? &validation : nullptr,
        .pApplicationInfo = &application,
        .enabledLayerCount = validate ? 1u : 0u,
        .ppEnabledLayerNames = validate ? &validationLayer : nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    checkVk(vkCreateInstance(&createInfo, nullptr, &instance), "Create Vulkan instance");
    if (validate)
    {
        const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (!create)
            throw std::runtime_error("Debug messenger entry point is unavailable.");
        checkVk(create(instance, &debugInfo, nullptr, &debugMessenger), "Create debug messenger");
    }
    checkVk(glfwCreateWindowSurface(instance, window, nullptr, &surface), "Create GLFW surface");

    uint32_t deviceCount = 0;
    checkVk(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "Count Vulkan devices");
    std::vector<VkPhysicalDevice> devices(deviceCount);
    checkVk(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()), "Enumerate Vulkan devices");
    for (auto candidate : devices)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3 || !supportsSwapchain(candidate))
            continue;
        VkPhysicalDeviceVulkan13Features features13{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        };
        VkPhysicalDeviceFeatures2 features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &features13,
        };
        vkGetPhysicalDeviceFeatures2(candidate, &features);
        constexpr auto color = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if (!features13.dynamicRendering || !features13.synchronization2 ||
            !supportsFormat(candidate, VK_FORMAT_R8G8B8A8_UNORM, color) ||
            !supportsFormat(candidate, VK_FORMAT_R16G16B16A16_SFLOAT, color) ||
            !supportsFormat(candidate, VK_FORMAT_D32_SFLOAT,
                            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
            continue;

        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
        for (uint32_t i = 0; i < count; ++i)
        {
            VkBool32 canPresent = VK_FALSE;
            checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &canPresent),
                    "Query presentation support");
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && canPresent)
            {
                physicalDevice = candidate;
                queueFamily = i;
                break;
            }
        }
        if (physicalDevice)
        {
            std::cout << "GPU: " << properties.deviceName << '\n';
            break;
        }
    }
    if (!physicalDevice)
        throw std::runtime_error("No Vulkan 1.3 device supports the required formats and presentation queue.");

    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queueInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    const VkPhysicalDeviceVulkan13Features enabled{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .shaderDemoteToHelperInvocation = VK_TRUE,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    constexpr const char* swapchain = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    const VkDeviceCreateInfo deviceInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = &swapchain,
    };
    checkVk(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device), "Create Vulkan device");
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamily,
    };
    checkVk(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool), "Create command pool");
    const VkCommandBufferAllocateInfo commandInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    checkVk(vkAllocateCommandBuffers(device, &commandInfo, &commands), "Allocate command buffer");
}

uint32_t VulkanContext::memoryType(uint32_t allowed, VkMemoryPropertyFlags required) const
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
    {
        if ((allowed & (1u << i)) && (properties.memoryTypes[i].propertyFlags & required) == required)
            return i;
    }
    throw std::runtime_error("No matching Vulkan memory type.");
}

void VulkanContext::createBuffer(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties) const
{
    buffer.device = device;
    const VkBufferCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    checkVk(vkCreateBuffer(device, &info, nullptr, &buffer.handle), "Create buffer");
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
    const VkMemoryAllocateInfo allocation{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memoryType(requirements.memoryTypeBits, properties),
    };
    checkVk(vkAllocateMemory(device, &allocation, nullptr, &buffer.memory), "Allocate buffer memory");
    checkVk(vkBindBufferMemory(device, buffer.handle, buffer.memory, 0), "Bind buffer memory");
}

VkImageView VulkanContext::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect,
                                          uint32_t mipLevels) const
{
    VkImageView view = VK_NULL_HANDLE;
    const VkImageViewCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {aspect, 0, mipLevels, 0, 1},
    };
    checkVk(vkCreateImageView(device, &info, nullptr, &view), "Create image view");
    return view;
}

void VulkanContext::createImage(Image& image, VkExtent2D extent, VkFormat format,
                                VkImageUsageFlags usage, VkImageAspectFlags aspect,
                                VkSampleCountFlagBits samples, uint32_t mipLevels) const
{
    image.device = device;
    const VkImageCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {extent.width, extent.height, 1},
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = samples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    checkVk(vkCreateImage(device, &info, nullptr, &image.handle), "Create image");
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image.handle, &requirements);
    const VkMemoryAllocateInfo allocation{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    checkVk(vkAllocateMemory(device, &allocation, nullptr, &image.memory), "Allocate image memory");
    checkVk(vkBindImageMemory(device, image.handle, image.memory, 0), "Bind image memory");
    image.view = createImageView(image.handle, format, aspect, mipLevels);
}

void VulkanContext::fillStaging(Buffer& staging, const void* data, VkDeviceSize size) const
{
    createBuffer(staging, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped = nullptr;
    checkVk(vkMapMemory(device, staging.memory, 0, size, 0, &mapped), "Map staging memory");
    std::memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(device, staging.memory);
}

void VulkanContext::beginUpload() const
{
    checkVk(vkResetCommandBuffer(commands, 0), "Reset upload command buffer");
    const VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    checkVk(vkBeginCommandBuffer(commands, &begin), "Begin upload");
}

void VulkanContext::endUpload() const
{
    checkVk(vkEndCommandBuffer(commands), "End upload");
    const VkCommandBufferSubmitInfo command{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = commands,
    };
    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &command,
    };
    checkVk(vkQueueSubmit2(queue, 1, &submit, VK_NULL_HANDLE), "Submit upload");
    checkVk(vkQueueWaitIdle(queue), "Wait for upload");
}

void VulkanContext::uploadBuffer(Buffer& target, const void* data, VkDeviceSize size,
                                 VkBufferUsageFlags usage, VkAccessFlags2 readAccess) const
{
    Buffer staging;
    fillStaging(staging, data, size);
    createBuffer(target, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    beginUpload();
    const VkBufferCopy copy{.size = size};
    vkCmdCopyBuffer(commands, staging.handle, target.handle, 1, &copy);
    const VkBufferMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
        .dstAccessMask = readAccess,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = target.handle,
        .size = VK_WHOLE_SIZE,
    };
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(commands, &dependency);
    endUpload();
}

void VulkanContext::uploadTexture(Image& target, VkExtent2D extent, VkFormat format, const void* rgba) const
{
    constexpr auto aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    constexpr auto copyStage = VK_PIPELINE_STAGE_2_COPY_BIT;
    constexpr auto blitStage = VK_PIPELINE_STAGE_2_BLIT_BIT;
    constexpr auto fragmentStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    constexpr auto mipFeatures = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    const uint32_t mipLevels = supportsFormat(physicalDevice, format, mipFeatures)
        ? std::bit_width(std::max(extent.width, extent.height)) : 1;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (mipLevels > 1)
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    Buffer staging;
    fillStaging(staging, rgba, VkDeviceSize(extent.width) * extent.height * 4);
    createImage(target, extent, format, usage, aspect, VK_SAMPLE_COUNT_1_BIT, mipLevels);
    beginUpload();
    transitionImage(commands, target.handle, aspect,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE, 0, copyStage | blitStage, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    0, mipLevels);
    const VkBufferImageCopy copy{
        .imageSubresource = {aspect, 0, 0, 1},
        .imageExtent = {extent.width, extent.height, 1},
    };
    vkCmdCopyBufferToImage(commands, staging.handle, target.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    VkOffset3D size{static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
    auto writeStage = copyStage;
    for (uint32_t level = 1; level < mipLevels; ++level)
    {
        transitionImage(commands, target.handle, aspect,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        writeStage, VK_ACCESS_2_TRANSFER_WRITE_BIT, blitStage, VK_ACCESS_2_TRANSFER_READ_BIT,
                        level - 1);
        const VkOffset3D nextSize{std::max(1, size.x / 2), std::max(1, size.y / 2), 1};
        const VkImageBlit blit{
            .srcSubresource = {aspect, level - 1, 0, 1},
            .srcOffsets = {{}, size},
            .dstSubresource = {aspect, level, 0, 1},
            .dstOffsets = {{}, nextSize},
        };
        vkCmdBlitImage(commands, target.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       target.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        transitionImage(commands, target.handle, aspect,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        blitStage, VK_ACCESS_2_TRANSFER_READ_BIT, fragmentStage, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                        level - 1);
        size = nextSize;
        writeStage = blitStage;
    }
    transitionImage(commands, target.handle, aspect,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    writeStage, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    fragmentStage, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, mipLevels - 1);
    endUpload();
}

void transitionImage(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect,
                     VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                     uint32_t baseMipLevel, uint32_t mipLevels)
{
    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {aspect, baseMipLevel, mipLevels, 0, 1},
    };
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(commands, &dependency);
}
