#include "Application.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <stdexcept>

void Application::initializeOverlay()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplGlfw_InitForVulkan(window.handle, true))
        throw std::runtime_error("Could not initialize the ImGui GLFW backend.");
}

void Application::configureOverlay(VkFormat format)
{
    if (format == overlayFormat)
        return;

    ImGui_ImplVulkan_PipelineInfo pipeline{};
    pipeline.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &format,
    };

    if (overlayFormat == VK_FORMAT_UNDEFINED)
    {
        ImGui_ImplVulkan_InitInfo init{};
        init.ApiVersion = VK_API_VERSION_1_3;
        init.Instance = vk.instance;
        init.PhysicalDevice = vk.physicalDevice;
        init.Device = vk.device;
        init.QueueFamily = vk.queueFamily;
        init.Queue = vk.queue;
        init.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;
        // The backend rotates its draw buffers once per submission, like the frame slots.
        init.MinImageCount = static_cast<uint32_t>(framesInFlight);
        init.ImageCount = static_cast<uint32_t>(framesInFlight);
        init.PipelineInfoMain = pipeline;
        init.UseDynamicRendering = true;
        init.CheckVkResultFn = [](VkResult result) { checkVk(result, "ImGui Vulkan backend"); };
        if (!ImGui_ImplVulkan_Init(&init))
            throw std::runtime_error("Could not initialize the ImGui Vulkan backend.");
    }
    else
    {
        ImGui_ImplVulkan_CreateMainPipeline(&pipeline);
    }
    overlayFormat = format;
}

void Application::shutdownOverlay()
{
    if (!ImGui::GetCurrentContext())
        return;
    const auto& io = ImGui::GetIO();
    if (io.BackendRendererUserData)
        ImGui_ImplVulkan_Shutdown();
    if (io.BackendPlatformUserData)
        ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Application::buildOverlay()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    constexpr float sampleInterval = 0.25f;
    const int historySize = static_cast<int>(fpsHistory.size());
    fpsSampleTime += ImGui::GetIO().DeltaTime;
    ++fpsFrameCount;
    if (fpsSampleTime >= sampleInterval)
    {
        fpsHistory[nextFpsSample] = static_cast<float>(fpsFrameCount) / fpsSampleTime;
        nextFpsSample = (nextFpsSample + 1) % historySize;
        fpsSampleCount = std::min(fpsSampleCount + 1, historySize);
        fpsSampleTime = 0.0f;
        fpsFrameCount = 0;
    }

    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Renderer"))
    {
        const float fps = fpsSampleCount ? fpsHistory[(nextFpsSample + historySize - 1) % historySize]
                                         : ImGui::GetIO().Framerate;
        ImGui::Text("%.1f FPS", fps);
        const int offset = fpsSampleCount == historySize ? nextFpsSample : 0;
        const float peak = std::max({60.0f, fps, *std::max_element(fpsHistory.begin(), fpsHistory.end())});
        ImGui::PlotLines("##fps", fpsHistory.data(), fpsSampleCount, offset, nullptr,
                         0.0f, peak * 1.1f, ImVec2(-1.0f, 80.0f));

        ImGui::SeparatorText("G-buffer view");
        ImGui::RadioButton("Lit", &debugView, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Base color", &debugView, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Normals", &debugView, 2);
        ImGui::RadioButton("Depth", &debugView, 3);
        ImGui::SameLine();
        ImGui::RadioButton("Metallic / roughness", &debugView, 4);
        if (ImGui::CollapsingHeader("Rasterization effects", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::CheckboxFlags("Ambient occlusion (SSAO)", &effects, ssao);
            ImGui::CheckboxFlags("Point light shadows", &effects, pointShadows);
            ImGui::CheckboxFlags("Reflections (SSR)", &effects, reflections);
            ImGui::CheckboxFlags("Toon shading and outlines", &effects, toon);
            ImGui::CheckboxFlags("FXAA", &effects, fxaa);
            ImGui::CheckboxFlags("Area light", &effects, areaLighting);
            ImGui::CheckboxFlags("Volumetric light", &effects, volumetricLighting);
        }
    }
    ImGui::End();
    ImGui::Render();
}
