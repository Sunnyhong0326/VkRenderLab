#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>

struct GLFWwindow;

void checkVk(VkResult result, const char* operation);

struct Buffer
{
    Buffer() = default;
    ~Buffer();
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    VkDevice device = VK_NULL_HANDLE;
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct Image
{
    Image() = default;
    ~Image();
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    VkDevice device = VK_NULL_HANDLE;
    VkImage handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

class VulkanContext
{
public:
    VulkanContext() = default;
    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    void initialize(GLFWwindow* window, bool validate);
    void createBuffer(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties) const;
    void createImage(Image& image, VkExtent2D extent, VkFormat format,
                     VkImageUsageFlags usage, VkImageAspectFlags aspect,
                     VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, uint32_t mipLevels = 1) const;
    void uploadBuffer(Buffer& target, const void* data, VkDeviceSize size,
                      VkBufferUsageFlags usage, VkAccessFlags2 readAccess) const;
    void uploadTexture(Image& target, VkExtent2D extent, VkFormat format, const void* rgba) const;
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect,
                                uint32_t mipLevels = 1) const;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    std::atomic_uint validationErrors{0};

private:
    uint32_t memoryType(uint32_t allowed, VkMemoryPropertyFlags properties) const;
    void beginUpload() const;
    void endUpload() const;
    void fillStaging(Buffer& staging, const void* data, VkDeviceSize size) const;

    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
};

void transitionImage(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect,
                     VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                     uint32_t baseMipLevel = 0, uint32_t mipLevels = 1);
