#include "Application.hpp"

#include <imgui.h>
#include "Pipeline.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>

namespace
{
constexpr VkFormat baseFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat normalFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
constexpr VkFormat screenFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr std::array gbufferFormats{baseFormat, normalFormat};
constexpr VkShaderStageFlags geometryStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
constexpr auto colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
constexpr auto depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

VkSampleCountFlagBits chooseGeometrySamples(VkPhysicalDevice device)
{
    const auto sampleCounts = [device](VkFormat format, VkImageUsageFlags usage) {
        VkImageFormatProperties properties{};
        const VkResult result = vkGetPhysicalDeviceImageFormatProperties(
            device, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &properties);
        if (result == VK_ERROR_FORMAT_NOT_SUPPORTED)
            return VkSampleCountFlags{0};
        checkVk(result, "Query G-buffer sample counts");
        return properties.sampleCounts;
    };
    const auto supported = sampleCounts(baseFormat, colorUsage) & sampleCounts(normalFormat, colorUsage) &
                           sampleCounts(depthFormat, depthUsage);
    for (const auto samples : {VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_2_BIT})
    {
        if (supported & samples)
            return samples;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

VkDescriptorSetLayout createSampledImageLayout(VkDevice device, uint32_t imageCount, bool uniforms = false)
{
    std::vector<VkDescriptorSetLayoutBinding> bindings(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        bindings[i] = {
            .binding = i,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    }
    if (uniforms)
        bindings.push_back({imageCount, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT});
    const VkDescriptorSetLayoutCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    checkVk(vkCreateDescriptorSetLayout(device, &info, nullptr, &layout), "Create descriptor set layout");
    return layout;
}

VkSampler createSampler(VkDevice device, VkFilter filter, VkSamplerAddressMode addressMode, bool useMipmaps)
{
    const VkSamplerCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = filter,
        .minFilter = filter,
        .mipmapMode = useMipmaps ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = addressMode,
        .addressModeV = addressMode,
        .addressModeW = addressMode,
        .maxLod = useMipmaps ? VK_LOD_CLAMP_NONE : 0.0f,
    };
    VkSampler sampler = VK_NULL_HANDLE;
    checkVk(vkCreateSampler(device, &info, nullptr, &sampler), "Create sampler");
    return sampler;
}

void writeImageSet(VkDevice device, VkDescriptorSet set, std::span<const VkDescriptorImageInfo> images)
{
    std::vector<VkWriteDescriptorSet> writes(images.size());
    for (uint32_t i = 0; i < writes.size(); ++i)
    {
        writes[i] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &images[i],
        };
    }
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

VkSurfaceFormatKHR chooseSurfaceFormat(std::span<const VkSurfaceFormatKHR> available)
{
    for (VkFormat format : {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB})
    {
        for (const auto& candidate : available)
        {
            if (candidate.format == format && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                return candidate;
        }
    }
    throw std::runtime_error("The surface does not expose an sRGB swapchain format.");
}
}

Application::Window::~Window()
{
    if (handle)
        glfwDestroyWindow(handle);
    if (initialized)
        glfwTerminate();
}

Application::~Application()
{
    if (!vk.device)
        return;
    vkDeviceWaitIdle(vk.device);
    shutdownOverlay();
    destroySwapchain();
    for (const auto& frame : frames)
    {
        vkDestroySemaphore(vk.device, frame.imageAvailable, nullptr);
        vkDestroyFence(vk.device, frame.finished, nullptr);
        if (frame.mappedUniforms)
            vkUnmapMemory(vk.device, frame.uniforms.memory);
    }
    vkDestroyPipeline(vk.device, compositePipeline, nullptr);
    vkDestroyPipeline(vk.device, lightingPipeline, nullptr);
    vkDestroyPipeline(vk.device, shadowPipeline, nullptr);
    vkDestroyPipeline(vk.device, geometryPipeline, nullptr);
    vkDestroyPipelineLayout(vk.device, effectsLayout, nullptr);
    vkDestroyPipelineLayout(vk.device, geometryLayout, nullptr);
    vkDestroyDescriptorPool(vk.device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(vk.device, effectsSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(vk.device, materialSetLayout, nullptr);
    vkDestroySampler(vk.device, attachmentSampler, nullptr);
    vkDestroySampler(vk.device, screenSampler, nullptr);
    vkDestroySampler(vk.device, materialSampler, nullptr);
}

void Application::run(const RunOptions& options)
{
    initialize(options);
    uint32_t framesDrawn = 0;
    double previous = glfwGetTime();
    while (!glfwWindowShouldClose(window.handle))
    {
        glfwPollEvents();
        const double now = glfwGetTime();
        updateCamera(static_cast<float>(std::min(now - previous, 0.05)));
        previous = now;
        if (drawFrame() && options.frameLimit && ++framesDrawn >= options.frameLimit)
            break;
    }
    checkVk(vkDeviceWaitIdle(vk.device), "Wait for device at shutdown");
    if (options.frameLimit)
        std::cout << "Rendered " << framesDrawn << " frame(s).\n";
    if (vk.validationErrors.load() > 0)
        throw std::runtime_error("Vulkan validation reported errors.");
}

void Application::initialize(const RunOptions& options)
{
    const Scene scene = options.scene.empty() ? makePracticeScene() : loadGltf(options.scene);
    std::cout << "Scene: " << (options.scene.empty() ? "practice scene" : options.scene.string())
              << " (" << scene.indices.size() / 3 << " triangles)\n";
    if (!glfwInit())
        throw std::runtime_error("Could not initialize GLFW.");
    window.initialized = true;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window.handle = glfwCreateWindow(1280, 720, "VkRenderLab", nullptr, nullptr);
    if (!window.handle)
        throw std::runtime_error("Could not create GLFW window.");
    glfwSetWindowUserPointer(window.handle, this);
    glfwSetFramebufferSizeCallback(window.handle, [](GLFWwindow* handle, int, int) {
        static_cast<Application*>(glfwGetWindowUserPointer(handle))->resized = true;
    });
    vk.initialize(window.handle, options.validate);
    geometrySamples = chooseGeometrySamples(vk.physicalDevice);
    std::cout << "Geometry samples: " << geometrySamples << '\n';

    const glm::vec3 target = options.scene.empty() ? glm::vec3(0.0f) : scene.center;
    camera.eye = options.scene.empty() ? glm::vec3(4.5f, 2.9f, 5.5f)
                                     : scene.center + scene.radius * glm::vec3(0.44f, -0.13f, 0.04f);
    const glm::vec3 forward = glm::normalize(target - camera.eye);
    camera.yaw = glm::atan(forward.z, forward.x);
    camera.pitch = glm::asin(forward.y);
    camera.speed = scene.radius * 0.25f;
    camera.farPlane = scene.radius * 8.0f;
    initializeLights(scene);

    createFrames();
    uploadScene(scene);
    createDescriptors(scene);
    geometryLayout = createPipelineLayout(vk.device, materialSetLayout, geometryStages, sizeof(GeometryPush));
    effectsLayout = createPipelineLayout(vk.device, effectsSetLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
    geometryPipeline = createPipeline(vk.device, geometryLayout, "geometry.vert.spv", "geometry.frag.spv",
                                      gbufferFormats, depthFormat, geometrySamples);
    shadowPipeline = createPipeline(vk.device, geometryLayout, "shadow.vert.spv", "shadow.frag.spv",
                                    {}, depthFormat);
    const bool multisampled = geometrySamples != VK_SAMPLE_COUNT_1_BIT;
    lightingPipeline = createPipeline(vk.device, effectsLayout, "lighting.vert.spv",
                                      multisampled ? "lighting_ms.frag.spv" : "lighting.frag.spv",
                                      std::array{screenFormat});
    compositePipeline = createPipeline(vk.device, effectsLayout, "lighting.vert.spv",
                                       multisampled ? "composite_ms.frag.spv" : "composite.frag.spv",
                                       std::array{screenFormat});
    initializeOverlay();
    createSwapchain();
}

void Application::initializeLights(const Scene& scene)
{
    const glm::vec3 sunDirection = glm::normalize(glm::vec3(0.55f, 1.0f, 0.35f));
    const glm::quat sunRotation(glm::vec3(glm::acos(sunDirection.y),
                                         glm::atan(sunDirection.x, sunDirection.z), 0.0f));
    lights = {
        {.id = nextLightId++, .type = LightType::directional, .rotation = sunRotation, .intensity = 1.25f},
        {.id = nextLightId++, .type = LightType::point, .extent = camera.farPlane * 0.16f},
        {.id = nextLightId++, .type = LightType::area,
         .position = scene.center + glm::vec3(0, scene.radius * 0.24f, 0),
         .extent = scene.radius * 0.10f},
    };
    cameraLightId = lights[1].id;
    shadowLightId = cameraLightId;
    selectedLightId = cameraLightId;
}

glm::vec3 Application::lightPosition(const Light& light) const
{
    if (light.id != cameraLightId)
        return light.position;
    const glm::vec3 up(0, 1, 0);
    const glm::vec3 right = glm::normalize(glm::cross(camera.forward(), up));
    return camera.eye + (right * 0.12f + up * 0.04f) * (camera.farPlane * 0.16f);
}

glm::vec3 Application::lightPlacementPosition() const
{
    return camera.eye + camera.forward() * (camera.farPlane * 0.06f);
}

void Application::createFrames()
{
    std::array<VkCommandBuffer, framesInFlight> commands{};
    const VkCommandBufferAllocateInfo allocate{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(commands.size()),
    };
    checkVk(vkAllocateCommandBuffers(vk.device, &allocate, commands.data()), "Allocate frame command buffers");

    const VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (size_t i = 0; i < frames.size(); ++i)
    {
        auto& frame = frames[i];
        frame.commands = commands[i];
        checkVk(vkCreateFence(vk.device, &fenceInfo, nullptr, &frame.finished), "Create frame fence");
        checkVk(vkCreateSemaphore(vk.device, &semaphoreInfo, nullptr, &frame.imageAvailable),
                "Create acquire semaphore");
        vk.createBuffer(frame.uniforms, sizeof(EffectsUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        checkVk(vkMapMemory(vk.device, frame.uniforms.memory, 0, sizeof(EffectsUniforms), 0, &frame.mappedUniforms),
                "Map frame uniforms");
        vk.createImage(frame.shadowAtlas, shadowAtlasExtent, depthFormat, depthUsage, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
}

void Application::uploadScene(const Scene& scene)
{
    vk.uploadBuffer(vertices, scene.vertices.data(), scene.vertices.size() * sizeof(Vertex),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
    vk.uploadBuffer(indices, scene.indices.data(), scene.indices.size() * sizeof(uint32_t),
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_ACCESS_2_INDEX_READ_BIT);
    for (const auto& texture : scene.textures)
    {
        const VkFormat format = texture.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        vk.uploadTexture(textures.emplace_back(), {texture.width, texture.height}, format, texture.rgba.data());
    }
    primitives = scene.primitives;
    materials = scene.materials;
}

void Application::createDescriptors(const Scene& scene)
{
    materialSampler = createSampler(vk.device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, true);
    attachmentSampler = createSampler(vk.device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
    screenSampler = createSampler(vk.device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
    constexpr uint32_t materialImageCount = 3;
    constexpr uint32_t frameImageCount = 6;
    materialSetLayout = createSampledImageLayout(vk.device, materialImageCount);
    effectsSetLayout = createSampledImageLayout(vk.device, frameImageCount, true);

    const auto setCount = static_cast<uint32_t>(scene.materials.size() + frames.size());
    const std::array sizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             static_cast<uint32_t>(scene.materials.size() * materialImageCount +
                                                   frames.size() * frameImageCount)},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(frames.size())},
    };
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = setCount,
        .poolSizeCount = static_cast<uint32_t>(sizes.size()),
        .pPoolSizes = sizes.data(),
    };
    checkVk(vkCreateDescriptorPool(vk.device, &poolInfo, nullptr, &descriptorPool), "Create descriptor pool");
    std::vector<VkDescriptorSetLayout> layouts(scene.materials.size(), materialSetLayout);
    layouts.resize(setCount, effectsSetLayout);
    std::vector<VkDescriptorSet> sets(setCount);
    const VkDescriptorSetAllocateInfo allocate{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = setCount,
        .pSetLayouts = layouts.data(),
    };
    checkVk(vkAllocateDescriptorSets(vk.device, &allocate, sets.data()), "Allocate descriptor sets");
    for (size_t i = 0; i < frames.size(); ++i)
    {
        auto& frame = frames[i];
        frame.effectsSet = sets[scene.materials.size() + i];
        const VkDescriptorBufferInfo buffer{frame.uniforms.handle, 0, sizeof(EffectsUniforms)};
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = frame.effectsSet,
            .dstBinding = frameImageCount,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &buffer,
        };
        vkUpdateDescriptorSets(vk.device, 1, &write, 0, nullptr);
    }
    sets.resize(scene.materials.size());
    materialSets = std::move(sets);

    for (size_t i = 0; i < scene.materials.size(); ++i)
    {
        const auto& material = scene.materials[i];
        const std::array<VkDescriptorImageInfo, materialImageCount> images{{
            {materialSampler, textures[material.baseTexture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {materialSampler, textures[material.surfaceTexture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {materialSampler, textures[material.normalTexture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        }};
        writeImageSet(vk.device, materialSets[i], images);
    }
}

void Application::createSwapchain()
{
    VkSurfaceCapabilitiesKHR capabilities;
    checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physicalDevice, vk.surface, &capabilities),
            "Query surface capabilities");
    uint32_t formatCount = 0;
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physicalDevice, vk.surface, &formatCount, nullptr),
            "Count surface formats");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physicalDevice, vk.surface, &formatCount, formats.data()),
            "Read surface formats");
    const auto surfaceFormat = chooseSurfaceFormat(formats);
    extent = capabilities.currentExtent;
    if (extent.width == std::numeric_limits<uint32_t>::max())
    {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window.handle, &width, &height);
        extent.width = std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
    }
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount)
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (auto candidate : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                           VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
    {
        if (capabilities.supportedCompositeAlpha & candidate)
        {
            alpha = candidate;
            break;
        }
    }
    const VkSwapchainCreateInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk.surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = alpha,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };
    checkVk(vkCreateSwapchainKHR(vk.device, &info, nullptr, &swapchain), "Create swapchain");
    checkVk(vkGetSwapchainImagesKHR(vk.device, swapchain, &imageCount, nullptr), "Count swapchain images");
    std::vector<VkImage> images(imageCount);
    checkVk(vkGetSwapchainImagesKHR(vk.device, swapchain, &imageCount, images.data()), "Get swapchain images");
    swapImages.resize(imageCount);
    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (size_t i = 0; i < swapImages.size(); ++i)
    {
        auto& image = swapImages[i];
        image.image = images[i];
        image.view = vk.createImageView(image.image, surfaceFormat.format, VK_IMAGE_ASPECT_COLOR_BIT);
        checkVk(vkCreateSemaphore(vk.device, &semaphoreInfo, nullptr, &image.renderFinished),
                "Create presentation semaphore");
    }

    for (auto& frame : frames)
    {
        frame.images = std::make_unique<FrameImages>();
        auto& gbuffer = *frame.images;
        vk.createImage(gbuffer.baseMetallic, extent, baseFormat, colorUsage,
                       VK_IMAGE_ASPECT_COLOR_BIT, geometrySamples);
        vk.createImage(gbuffer.normalRoughness, extent, normalFormat, colorUsage,
                       VK_IMAGE_ASPECT_COLOR_BIT, geometrySamples);
        vk.createImage(gbuffer.depth, extent, depthFormat, depthUsage,
                       VK_IMAGE_ASPECT_DEPTH_BIT, geometrySamples);
        vk.createImage(gbuffer.litColor, extent, screenFormat, colorUsage, VK_IMAGE_ASPECT_COLOR_BIT);
        vk.createImage(gbuffer.compositeColor, extent, screenFormat, colorUsage, VK_IMAGE_ASPECT_COLOR_BIT);
        const std::array<VkDescriptorImageInfo, 6> descriptors{{
            {attachmentSampler, gbuffer.baseMetallic.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {attachmentSampler, gbuffer.normalRoughness.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {attachmentSampler, gbuffer.depth.view, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL},
            {attachmentSampler, frame.shadowAtlas.view, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL},
            {screenSampler, gbuffer.litColor.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {screenSampler, gbuffer.compositeColor.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        }};
        writeImageSet(vk.device, frame.effectsSet, descriptors);
    }
    presentPipeline = createPipeline(vk.device, effectsLayout, "lighting.vert.spv", "present.frag.spv",
                                      std::array{surfaceFormat.format});
    configureOverlay(surfaceFormat.format);
}

void Application::destroySwapchain()
{
    vkDestroyPipeline(vk.device, presentPipeline, nullptr);
    presentPipeline = VK_NULL_HANDLE;
    for (auto& frame : frames)
        frame.images.reset();
    for (const auto& image : swapImages)
    {
        vkDestroySemaphore(vk.device, image.renderFinished, nullptr);
        vkDestroyImageView(vk.device, image.view, nullptr);
    }
    swapImages.clear();
    vkDestroySwapchainKHR(vk.device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

bool Application::recreateSwapchain()
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window.handle, &width, &height);
    while ((width == 0 || height == 0) && !glfwWindowShouldClose(window.handle))
    {
        glfwWaitEvents();
        glfwGetFramebufferSize(window.handle, &width, &height);
    }
    if (glfwWindowShouldClose(window.handle))
        return false;

    checkVk(vkDeviceWaitIdle(vk.device), "Wait for swapchain users");
    destroySwapchain();
    createSwapchain();
    resized = false;
    return true;
}

glm::vec3 Application::Camera::forward() const
{
    return {glm::cos(pitch) * glm::cos(yaw), glm::sin(pitch), glm::cos(pitch) * glm::sin(yaw)};
}

void Application::updateCamera(float deltaTime)
{
    auto* handle = window.handle;
    const auto key = [handle](int code) { return glfwGetKey(handle, code) == GLFW_PRESS; };
    const auto& ui = ImGui::GetIO();
    if (key(GLFW_KEY_ESCAPE))
        glfwSetWindowShouldClose(handle, GLFW_TRUE);
    if (!ui.WantCaptureKeyboard)
    {
        for (int i = 0; i < 5; ++i)
        {
            if (key(GLFW_KEY_1 + i))
                debugView = i;
        }
    }
    const bool lightKey = key(GLFW_KEY_L);
    if (!ui.WantCaptureKeyboard && lightKey && !lightKeyWasDown)
    {
        const auto mounted = std::ranges::find(lights, cameraLightId, &Light::id);
        if (mounted != lights.end())
            mounted->enabled = !mounted->enabled;
    }
    lightKeyWasDown = lightKey;

    const bool mouseDown = !draggingLightGizmo && glfwGetMouseButton(handle, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS &&
                           (looking || !ui.WantCaptureMouse);
    if (mouseDown != looking)
    {
        glfwSetInputMode(handle, GLFW_CURSOR, mouseDown ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        glfwGetCursorPos(handle, &previousCursor.x, &previousCursor.y);
        looking = mouseDown;
    }
    if (looking)
    {
        glm::dvec2 cursor;
        glfwGetCursorPos(handle, &cursor.x, &cursor.y);
        const glm::vec2 delta(cursor - previousCursor);
        previousCursor = cursor;
        camera.yaw += delta.x * 0.0025f;
        camera.pitch = glm::clamp(camera.pitch - delta.y * 0.0025f, -1.55f, 1.55f);
    }

    if (ui.WantCaptureKeyboard || draggingLightGizmo)
        return;

    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 forward = camera.forward();
    const glm::vec3 right = glm::normalize(glm::cross(forward, up));
    glm::vec3 motion = forward * float(key(GLFW_KEY_W) - key(GLFW_KEY_S)) +
                       right * float(key(GLFW_KEY_D) - key(GLFW_KEY_A)) +
                       up * float(key(GLFW_KEY_E) - key(GLFW_KEY_Q));
    if (glm::dot(motion, motion) > 0.0f)
    {
        const float boost = key(GLFW_KEY_LEFT_SHIFT) ? 3.0f : 1.0f;
        camera.eye += glm::normalize(motion) * camera.speed * boost * deltaTime;
    }
}
