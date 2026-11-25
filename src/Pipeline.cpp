#include "Pipeline.hpp"
#include "Scene.hpp"
#include "VulkanContext.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace
{
class ShaderModule
{
public:
    ShaderModule(VkDevice device, std::string_view filename) : device(device)
    {
        const auto path = std::filesystem::path(VULKAN_SHADER_DIR) / filename;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        const auto size = file.tellg();
        if (size <= 0 || size % sizeof(uint32_t) != 0)
            throw std::runtime_error("Invalid shader binary: " + path.string());
        std::vector<uint32_t> code(static_cast<size_t>(size) / sizeof(uint32_t));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(code.data()), size))
            throw std::runtime_error("Could not read shader binary: " + path.string());
        const VkShaderModuleCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = code.size() * sizeof(uint32_t),
            .pCode = code.data(),
        };
        checkVk(vkCreateShaderModule(device, &info, nullptr, &handle), "Create shader module");
    }

    ~ShaderModule() { vkDestroyShaderModule(device, handle, nullptr); }
    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    VkDevice device;
    VkShaderModule handle = VK_NULL_HANDLE;
};
}

VkPipelineLayout createPipelineLayout(VkDevice device, VkDescriptorSetLayout setLayout,
                                      VkShaderStageFlags stages, uint32_t pushSize)
{
    const VkPushConstantRange push{.stageFlags = stages, .size = pushSize};
    const VkPipelineLayoutCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &setLayout,
        .pushConstantRangeCount = pushSize ? 1u : 0u,
        .pPushConstantRanges = pushSize ? &push : nullptr,
    };
    VkPipelineLayout layout = VK_NULL_HANDLE;
    checkVk(vkCreatePipelineLayout(device, &info, nullptr, &layout), "Create pipeline layout");
    return layout;
}

VkPipeline createPipeline(VkDevice device, VkPipelineLayout layout, std::string_view vertexShader,
                          std::string_view fragmentShader, std::span<const VkFormat> colorFormats,
                          VkFormat depthFormat, VkSampleCountFlagBits sampleCount)
{
    const bool geometry = depthFormat != VK_FORMAT_UNDEFINED;
    const bool depthOnly = geometry && colorFormats.empty();
    const ShaderModule vertex(device, vertexShader);
    const ShaderModule fragment(device, fragmentShader);
    const std::array stages{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex.handle, .pName = "main",
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment.handle, .pName = "main",
        },
    };
    const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 3> attributes{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
    }};
    const VkPipelineVertexInputStateCreateInfo input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = geometry ? 1u : 0u,
        .pVertexBindingDescriptions = geometry ? &binding : nullptr,
        .vertexAttributeDescriptionCount = depthOnly ? 2u : geometry ? 3u : 0u,
        .pVertexAttributeDescriptions = geometry ? attributes.data() : nullptr,
    };
    const VkPipelineInputAssemblyStateCreateInfo assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewport{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo raster{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo samples{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = sampleCount,
    };
    const VkPipelineDepthStencilStateCreateInfo depth{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = geometry,
        .depthWriteEnable = geometry,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };
    const std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(colorFormats.size(), {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    });
    const VkPipelineColorBlendStateCreateInfo blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = static_cast<uint32_t>(blendAttachments.size()),
        .pAttachments = blendAttachments.data(),
    };
    constexpr std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    const VkPipelineRenderingCreateInfo rendering{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = static_cast<uint32_t>(colorFormats.size()),
        .pColorAttachmentFormats = colorFormats.data(),
        .depthAttachmentFormat = depthFormat,
    };
    const VkGraphicsPipelineCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = static_cast<uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &input,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &samples,
        .pDepthStencilState = &depth,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = layout,
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    checkVk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
            "Create graphics pipeline");
    return pipeline;
}
