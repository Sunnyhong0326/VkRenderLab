#include "Application.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <array>
#include <cstring>
#include <limits>

namespace
{
constexpr auto waitForever = std::numeric_limits<uint64_t>::max();
constexpr auto colorStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
constexpr auto depthStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
constexpr auto fragmentStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

VkRenderingAttachmentInfo attachment(VkImageView view, VkImageLayout layout, VkClearValue clear)
{
    return {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = view,
        .imageLayout = layout,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clear,
    };
}
}

bool Application::drawFrame()
{
    if (resized && !recreateSwapchain())
        return false;

    const auto& frame = frames[frameIndex];
    checkVk(vkWaitForFences(vk.device, 1, &frame.finished, VK_TRUE, waitForever), "Wait for frame");
    uint32_t imageIndex = 0;
    const VkResult acquire =
        vkAcquireNextImageKHR(vk.device, swapchain, waitForever, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapchain();
        return false;
    }
    if (acquire != VK_SUBOPTIMAL_KHR)
        checkVk(acquire, "Acquire swapchain image");

    buildOverlay();
    recordFrame(frame, imageIndex);

    // Reacquiring this image ensures its previous present has consumed this semaphore.
    const VkSemaphore renderFinished = swapImages[imageIndex].renderFinished;
    const VkSemaphoreSubmitInfo wait{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.imageAvailable,
        .stageMask = colorStage,
    };
    const VkSemaphoreSubmitInfo signal{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = renderFinished,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    const VkCommandBufferSubmitInfo commands{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.commands,
    };
    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &wait,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commands,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signal,
    };
    checkVk(vkResetFences(vk.device, 1, &frame.finished), "Reset frame fence");
    checkVk(vkQueueSubmit2(vk.queue, 1, &submit, frame.finished), "Submit frame");
    frameIndex = (frameIndex + 1) % frames.size();

    const VkPresentInfoKHR present{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderFinished,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &imageIndex,
    };
    const VkResult result = vkQueuePresentKHR(vk.queue, &present);
    if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
        checkVk(result, "Present frame");
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquire == VK_SUBOPTIMAL_KHR)
        recreateSwapchain();
    return result != VK_ERROR_OUT_OF_DATE_KHR;
}

void Application::recordFrame(const Frame& frame, uint32_t imageIndex)
{
    const auto commands = frame.commands;
    checkVk(vkResetCommandBuffer(commands, 0), "Reset frame command buffer");
    const VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    checkVk(vkBeginCommandBuffer(commands, &begin), "Begin frame command buffer");

    const glm::mat4 view = glm::lookAt(camera.eye, camera.eye + camera.forward(), glm::vec3(0, 1, 0));
    glm::mat4 projection = glm::perspective(glm::radians(62.0f), float(extent.width) / float(extent.height),
                                           camera.farPlane / 4000.0f, camera.farPlane);
    projection[1][1] *= -1.0f;
    const glm::mat4 viewProjection = projection * view;
    const auto uniforms = makeEffectsUniforms(viewProjection);
    std::memcpy(frame.mappedUniforms, &uniforms, sizeof(uniforms));

    recordShadowPass(frame, uniforms);
    const VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    recordGeometryPass(frame, viewProjection);
    recordScreenPass(frame, frame.images->litColor, lightingPipeline);
    recordScreenPass(frame, frame.images->compositeColor, compositePipeline);
    recordPresentPass(frame, imageIndex);
    checkVk(vkEndCommandBuffer(commands), "End frame command buffer");
}

Application::EffectsUniforms Application::makeEffectsUniforms(const glm::mat4& viewProjection) const
{
    const glm::vec3 up(0, 1, 0);
    const glm::vec3 right = glm::normalize(glm::cross(camera.forward(), up));
    const float pointRadius = camera.farPlane * 0.16f;
    const glm::vec3 point = camera.eye + (right * 0.12f + up * 0.04f) * pointRadius;
    EffectsUniforms uniforms{
        .viewProjection = viewProjection,
        .inverseViewProjection = glm::inverse(viewProjection),
        .eye = glm::vec4(camera.eye, 1.0f),
        .pointLight = glm::vec4(point, pointRadius),
        .areaLight = sceneAreaLight,
        .settings = glm::ivec4(debugView, lightEnabled, effects, 0),
    };
    constexpr std::array<glm::vec3, 6> directions{{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    }};
    constexpr std::array<glm::vec3, 6> faceUp{{
        {0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0},
    }};
    const auto projection = glm::perspective(glm::radians(90.0f), 1.0f, pointRadius * 0.0125f, pointRadius);
    for (size_t i = 0; i < directions.size(); ++i)
        uniforms.shadowViewProjection[i] = projection * glm::lookAt(point, point + directions[i], faceUp[i]);
    return uniforms;
}

void Application::recordShadowPass(const Frame& frame, const EffectsUniforms& uniforms)
{
    const auto commands = frame.commands;
    const auto& atlas = frame.shadowAtlas;
    const bool drawShadows = debugView == 0 && lightEnabled && (effects & pointShadows);
    if (!drawShadows)
    {
        transitionImage(commands, atlas.handle, VK_IMAGE_ASPECT_DEPTH_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_NONE, 0, fragmentStage, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        return;
    }
    transitionImage(commands, atlas.handle, VK_IMAGE_ASPECT_DEPTH_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE, 0, depthStages,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    const auto depth = attachment(atlas.view, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                  VkClearValue{.depthStencil = {1.0f, 0}});
    const VkRenderingInfo render{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, shadowAtlasExtent},
        .layerCount = 1,
        .pDepthAttachment = &depth,
    };
    vkCmdBeginRendering(commands, &render);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
    for (uint32_t face = 0; face < uniforms.shadowViewProjection.size(); ++face)
    {
        const VkOffset2D offset{
            static_cast<int32_t>(face % 3 * shadowFaceSize),
            static_cast<int32_t>(face / 3 * shadowFaceSize),
        };
        const VkViewport viewport{float(offset.x), float(offset.y), float(shadowFaceSize),
                                  float(shadowFaceSize), 0, 1};
        const VkRect2D scissor{offset, {shadowFaceSize, shadowFaceSize}};
        vkCmdSetViewport(commands, 0, 1, &viewport);
        vkCmdSetScissor(commands, 0, 1, &scissor);
        drawScene(commands, uniforms.shadowViewProjection[face]);
    }
    vkCmdEndRendering(commands);
    transitionImage(commands, atlas.handle, VK_IMAGE_ASPECT_DEPTH_BIT,
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                    depthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    fragmentStage, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void Application::drawScene(VkCommandBuffer commands, const glm::mat4& viewProjection)
{
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commands, 0, 1, &vertices.handle, &offset);
    vkCmdBindIndexBuffer(commands, indices.handle, 0, VK_INDEX_TYPE_UINT32);
    for (const auto& primitive : primitives)
    {
        const auto& material = materials[primitive.material];
        const GeometryPush push{viewProjection, material.baseColor, material.parameters};
        vkCmdPushConstants(commands, geometryLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryLayout, 0, 1,
                                &materialSets[primitive.material], 0, nullptr);
        vkCmdDrawIndexed(commands, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
    }
}

void Application::recordGeometryPass(const Frame& frame, const glm::mat4& viewProjection)
{
    const auto commands = frame.commands;
    const auto& gbuffer = *frame.images;
    const std::array colorImages{&gbuffer.baseMetallic, &gbuffer.normalRoughness};
    // The frame fence protects reuse. The next pass clears every attachment.
    for (const auto* image : colorImages)
    {
        transitionImage(commands, image->handle, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_2_NONE, 0, colorStage, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }
    transitionImage(commands, gbuffer.depth.handle, VK_IMAGE_ASPECT_DEPTH_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE, 0, depthStages,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    const std::array colors{
        attachment(colorImages[0]->view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, {}),
        attachment(colorImages[1]->view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, {}),
    };
    const auto depth = attachment(gbuffer.depth.view, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                  VkClearValue{.depthStencil = {1.0f, 0}});
    const VkRenderingInfo render{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, extent},
        .layerCount = 1,
        .colorAttachmentCount = static_cast<uint32_t>(colors.size()),
        .pColorAttachments = colors.data(),
        .pDepthAttachment = &depth,
    };
    vkCmdBeginRendering(commands, &render);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryPipeline);
    drawScene(commands, viewProjection);
    vkCmdEndRendering(commands);

    for (const auto* image : colorImages)
    {
        transitionImage(commands, image->handle, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        colorStage, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        fragmentStage, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    transitionImage(commands, gbuffer.depth.handle, VK_IMAGE_ASPECT_DEPTH_BIT,
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                    depthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    fragmentStage, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void Application::recordScreenPass(const Frame& frame, const Image& target, VkPipeline pipeline)
{
    transitionImage(frame.commands, target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE, 0, colorStage, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    drawFullscreen(frame, target.view, pipeline, false);
    transitionImage(frame.commands, target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    colorStage, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, fragmentStage, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void Application::recordPresentPass(const Frame& frame, uint32_t imageIndex)
{
    const auto commands = frame.commands;
    const auto& image = swapImages[imageIndex];
    transitionImage(commands, image.image, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    colorStage, 0, colorStage, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    drawFullscreen(frame, image.view, presentPipeline, true);
    transitionImage(commands, image.image, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    colorStage, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
}

void Application::drawFullscreen(const Frame& frame, VkImageView target, VkPipeline pipeline, bool drawOverlay)
{
    const auto commands = frame.commands;
    auto color = attachment(target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, {});
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    const VkRenderingInfo render{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    };
    vkCmdBeginRendering(commands, &render);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, effectsLayout, 0, 1,
                            &frame.effectsSet, 0, nullptr);
    vkCmdDraw(commands, 3, 1, 0, 0);
    if (drawOverlay)
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commands);
    vkCmdEndRendering(commands);
}
