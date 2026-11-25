#include "Application.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <array>
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

    const VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    const glm::mat4 view = glm::lookAt(camera.eye, camera.eye + camera.forward(), glm::vec3(0, 1, 0));
    glm::mat4 projection = glm::perspective(glm::radians(62.0f), float(extent.width) / float(extent.height),
                                           camera.farPlane / 4000.0f, camera.farPlane);
    projection[1][1] *= -1.0f;
    const glm::mat4 viewProjection = projection * view;
    recordGeometryPass(frame, viewProjection);
    recordLightingPass(frame, imageIndex, glm::inverse(viewProjection));
    checkVk(vkEndCommandBuffer(commands), "End frame command buffer");
}

void Application::recordGeometryPass(const Frame& frame, const glm::mat4& viewProjection)
{
    const auto commands = frame.commands;
    const auto& gbuffer = *frame.gbuffer;
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

void Application::recordLightingPass(const Frame& frame, uint32_t imageIndex, const glm::mat4& inverseViewProjection)
{
    const auto commands = frame.commands;
    const auto& image = swapImages[imageIndex];
    transitionImage(commands, image.image, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    colorStage, 0, colorStage, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    const auto color = attachment(image.view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, {});
    const VkRenderingInfo render{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    };
    const LightingPush push{
        inverseViewProjection,
        glm::vec4(camera.eye, 1.0f),
        glm::vec4(camera.eye + glm::vec3(0.0f, 0.2f, 0.0f), camera.farPlane * 0.16f),
        glm::vec4(float(debugView), lightEnabled ? 1.0f : 0.0f, 0.0f, 0.0f),
    };
    vkCmdBeginRendering(commands, &render);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPipeline);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingLayout, 0, 1,
                            &frame.lightingSet, 0, nullptr);
    vkCmdPushConstants(commands, lightingLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(commands, 3, 1, 0, 0);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commands);
    vkCmdEndRendering(commands);
    transitionImage(commands, image.image, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    colorStage, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
}
