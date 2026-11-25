#pragma once

#include <vulkan/vulkan.h>

#include <span>
#include <string_view>

VkPipelineLayout createPipelineLayout(VkDevice device, VkDescriptorSetLayout setLayout,
                                      VkShaderStageFlags stages, uint32_t pushSize);
VkPipeline createPipeline(VkDevice device, VkPipelineLayout layout, std::string_view vertexShader,
                          std::string_view fragmentShader, std::span<const VkFormat> colorFormats,
                          VkFormat depthFormat = VK_FORMAT_UNDEFINED,
                          VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT);
