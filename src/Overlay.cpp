#include "Application.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

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

bool Application::buildLightEditor()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(std::max(16.0f, display.x - 356.0f), 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Lights"))
    {
        ImGui::End();
        return false;
    }

    const auto typeName = [](LightType type) {
        switch (type)
        {
            case LightType::directional: return "Directional";
            case LightType::point: return "Point";
            case LightType::area: return "Area";
        }
        return "";
    };
    const auto label = [&typeName](const Light& light) {
        return std::string(typeName(light.type)) + " " + std::to_string(light.id);
    };
    const float editStep = camera.farPlane * 0.001f;
    const float minExtent = camera.farPlane * 0.0001f;
    ImGui::BeginDisabled(lights.size() >= MAX_SCENE_LIGHTS);
    if (ImGui::Button("Add light"))
        ImGui::OpenPopup("Add light type");
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("Add light type"))
    {
        for (const auto type : {LightType::point, LightType::area, LightType::directional})
        {
            if (!ImGui::MenuItem(typeName(type)))
                continue;
            const float size = camera.farPlane * (type == LightType::area ? 0.018f : 0.16f);
            lights.push_back({.id = nextLightId++, .type = type, .position = lightPlacementPosition(), .extent = size});
            selectedLightId = lights.back().id;
            if (type == LightType::area)
                effects |= areaLighting;
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(showLightPositions ? "Hide light positions###light-positions"
                                        : "Show light positions###light-positions"))
        showLightPositions = !showLightPositions;

    auto selected = std::ranges::find(lights, selectedLightId, &Light::id);
    if (selected != lights.end())
    {
        const std::string currentLabel = label(*selected);
        if (ImGui::BeginCombo("Light", currentLabel.c_str()))
        {
            for (const auto& light : lights)
            {
                const bool current = light.id == selectedLightId;
                if (ImGui::Selectable(label(light).c_str(), current))
                    selectedLightId = light.id;
                if (current)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
            selected = std::ranges::find(lights, selectedLightId, &Light::id);
        }

        Light& light = *selected;
        ImGui::PushID(static_cast<int>(light.id));
        ImGui::Checkbox("Enabled", &light.enabled);
        if (ImGui::ColorEdit3("Color", &light.color.x))
            light.color = glm::clamp(light.color, glm::vec3(0.0f), glm::vec3(1.0f));
        ImGui::DragFloat("Intensity", &light.intensity, 0.02f, 0.0f, 25.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

        if (light.type != LightType::directional)
        {
            const bool attached = light.id == cameraLightId;
            if (ImGui::Button("Place in front of camera"))
            {
                if (attached)
                    cameraLightId = 0;
                light.position = lightPlacementPosition();
            }
            const glm::vec3 position = lightPosition(light);
            ImGui::Text("Position  %.2f, %.2f, %.2f", position.x, position.y, position.z);
            ImGui::DragFloat(light.type == LightType::point ? "Radius" : "Half-width",
                             &light.extent, editStep, minExtent, camera.farPlane, "%.3f",
                             ImGuiSliderFlags_AlwaysClamp);
        }
        if (light.type == LightType::point)
        {
            bool follow = light.id == cameraLightId;
            if (ImGui::Checkbox("Follow camera", &follow))
            {
                const auto previous = std::ranges::find(lights, cameraLightId, &Light::id);
                if (previous != lights.end())
                    previous->position = lightPosition(*previous);
                cameraLightId = follow ? light.id : 0;
            }
            bool shadowSource = light.id == shadowLightId;
            if (ImGui::Checkbox("Shadow and volume source", &shadowSource))
                shadowLightId = shadowSource ? light.id : 0;
            if (follow)
                ImGui::TextDisabled("Place the light to use the move gizmo.");
        }
        else
        {
            if (light.type == LightType::area)
            {
                if (ImGui::RadioButton("Move", lightGizmoMode == LightGizmoMode::move))
                    lightGizmoMode = LightGizmoMode::move;
                ImGui::SameLine();
                if (ImGui::RadioButton("Rotate", lightGizmoMode == LightGizmoMode::rotate))
                    lightGizmoMode = LightGizmoMode::rotate;
            }
            const glm::vec3 degrees = glm::degrees(glm::eulerAngles(light.rotation));
            ImGui::Text("Rotation  %.1f, %.1f, %.1f deg", degrees.x, degrees.y, degrees.z);
            if (light.type == LightType::area && !(effects & areaLighting))
                ImGui::TextDisabled("Area lighting is disabled in Renderer.");
        }

        if (ImGui::Button("Remove light"))
        {
            if (light.id == cameraLightId)
                cameraLightId = 0;
            if (light.id == shadowLightId)
                shadowLightId = 0;
            selected = lights.erase(selected);
            if (selected != lights.end())
                selectedLightId = selected->id;
            else
                selectedLightId = lights.empty() ? 0 : lights.back().id;
        }
        ImGui::PopID();
    }
    ImGui::End();
    return true;
}

void Application::drawLightViewport(bool editSelectedLight)
{
    const bool wasDragging = draggingLightGizmo;
    draggingLightGizmo = false;
    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    if (!extent.width || !extent.height || viewport.Size.x <= 0 || viewport.Size.y <= 0)
    {
        ImGuizmo::Enable(false);
        return;
    }

    const glm::mat4 view = cameraView();
    const glm::mat4 projection = cameraProjection();
    const glm::mat4 viewProjection = projection * view;
    const auto selected = std::ranges::find(lights, selectedLightId, &Light::id);
    if (editSelectedLight && selected != lights.end() && selected->id != cameraLightId && !looking)
    {
        Light& light = *selected;
        const bool directional = light.type == LightType::directional;
        const bool rotate = directional || (light.type == LightType::area && lightGizmoMode == LightGizmoMode::rotate);
        const glm::vec3 pivot = directional ? lightPlacementPosition() : light.position;
        const glm::vec4 clip = viewProjection * glm::vec4(pivot, 1.0f);
        // ImGuizmo 1.10 returns before restoring the draw clip rect when clip.z is too small.
        if (wasDragging || (clip.w > 0.0f && clip.z >= 0.001f && clip.z <= clip.w))
        {
            glm::mat4 transform = glm::mat4_cast(light.rotation);
            transform[3] = glm::vec4(pivot, 1.0f);
            ImGuizmo::Enable(true);
            ImGuizmo::SetRect(viewport.Pos.x, viewport.Pos.y, viewport.Size.x, viewport.Size.y);
            ImGuizmo::PushID(static_cast<int>(light.id));
            const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                rotate ? ImGuizmo::ROTATE : ImGuizmo::TRANSLATE, rotate ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
                glm::value_ptr(transform));
            draggingLightGizmo = ImGuizmo::IsUsing() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            if (changed || draggingLightGizmo)
            {
                if (rotate)
                    light.rotation = glm::normalize(glm::quat_cast(glm::mat3(transform)));
                else
                    light.position = glm::vec3(transform[3]);
            }
            ImGuizmo::PopID();
        }
    }
    if (!draggingLightGizmo)
        ImGuizmo::Enable(false);

    if (!showLightPositions)
        return;
    ImDrawList& draw = *ImGui::GetBackgroundDrawList();
    for (const Light& light : lights)
    {
        if (light.type == LightType::directional)
            continue;
        const glm::vec4 clip = viewProjection * glm::vec4(lightPosition(light), 1.0f);
        if (clip.w <= 0.0f || clip.z < 0.0f || clip.z > clip.w ||
            glm::abs(clip.x) > clip.w || glm::abs(clip.y) > clip.w)
            continue;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        const ImVec2 marker(viewport.Pos.x + (0.5f + 0.5f * ndc.x) * viewport.Size.x,
                            viewport.Pos.y + (0.5f - 0.5f * ndc.y) * viewport.Size.y);
        const float radius = light.id == selectedLightId ? 8.0f : 5.0f;
        draw.AddCircle(marker, radius, IM_COL32(0, 0, 0, 255), 0, 4.0f);
        draw.AddCircle(marker, radius, ImGui::ColorConvertFloat4ToU32(
            ImVec4(light.color.r, light.color.g, light.color.b, 1.0f)), 0, 2.0f);
    }
}

void Application::buildOverlay()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

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
            ImGui::CheckboxFlags("Area lights", &effects, areaLighting);
            ImGui::CheckboxFlags("Volumetric light", &effects, volumetricLighting);
        }
    }
    ImGui::End();
    const bool editSelectedLight = buildLightEditor();
    drawLightViewport(editSelectedLight);
    ImGui::Render();
}
