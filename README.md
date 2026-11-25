# VkRenderLab

VkRenderLab is a Vulkan 1.3 deferred renderer. It draws a built-in practice scene, loads static glTF/glb scenes, and lets you inspect the G-buffer.

## Build and run on Windows

Install Visual Studio Build Tools with the C++ workload, CMake, the Vulkan SDK, and vcpkg. Set `VCPKG_ROOT` to your vcpkg directory. CMake uses `vcpkg.json` to install dependencies, including Dear ImGui with the GLFW and Vulkan backends. CMake compiles GLSL to SPIR-V with the SDK's `glslc`.

```powershell
cmake -S . -B build "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Debug
.\build\Debug\vk_render_lab.exe --validate
```

Use `--frames 8` to run a short rendering check. `--validate` enables the Khronos validation layer and synchronization validation. If the loader reports that the layer is missing and your SDK includes it, set `$env:VK_LAYER_PATH = Join-Path $env:VULKAN_SDK 'Bin'` before running.

If you already have GLFW, GLM, stb, and `imgui[glfw-binding,vulkan-binding]` installed with vcpkg and a local copy of `cgltf.h`, you can use them without installing manifest dependencies. On this machine, use a fresh build directory with these options.

```powershell
cmake -S . -B build/local "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_MANIFEST_MODE=OFF "-DCGLTF_INCLUDE_DIR=D:/Projects/VulkanLearning/nvpro_core2/third_party/cgltf"
cmake --build build/local --config Debug
.\build\local\Debug\vk_render_lab.exe --validate
```

## Load Sponza

Put `Sponza.gltf` and its companion buffer and texture files together in `assets/sponza/`. Keep the relative paths from the original download. See the [Sponza files](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza/glTF) and [asset license](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/Sponza/LICENSE.md).

```powershell
.\build\Debug\vk_render_lab.exe --validate .\assets\sponza\Sponza.gltf
```

You can also pass the path to another static triangle-mesh `.gltf` or `.glb` model. The loader supports UV set 0, external PNG and JPEG textures, and images embedded as GLB buffer views. It skips alpha-blended primitives.

| Control | Action |
| --- | --- |
| Right mouse button and mouse movement | Look around |
| W / A / S / D | Move |
| Q / E | Move down / up |
| Left Shift | Move faster |
| 1 / 2 / 3 / 4 / 5 | Lit / base color / normals / depth / metallic and roughness |
| L | Toggle the camera light |
| Esc | Close the window |

The **Renderer** overlay shows a live FPS plot. Select a **G-buffer view** in the overlay or press `1` through `5` to switch views.

## Rasterization

Use **Renderer → Rasterization effects** in the **Lit** view to switch each effect on or off. SSAO, point light shadows, and FXAA start enabled. Press `L` to toggle the camera-mounted point light, which supplies the point shadows and volumetric lighting. The practice scene has a reflective floor for testing SSR.

| Feature | Implementation |
| --- | --- |
| Screen-space ambient occlusion (SSAO) | `ambientVisibility` samples nearby G-buffer depths and attenuates ambient light. [lighting.frag](shaders/lighting.frag) |
| Point light shadows | `recordShadowPass` renders six views to a depth atlas. The shadow shaders honor material alpha cutoffs, and `pointVisibility` filters the atlas when shading surfaces. [Frame.cpp](src/Frame.cpp), [shadow.vert](shaders/shadow.vert), [shadow.frag](shaders/shadow.frag), [effects.glsl](shaders/effects.glsl) |
| Screen-space reflections (SSR) | `screenReflection` marches reflected rays through the visible depth buffer and samples the lit image where they hit. [composite.frag](shaders/composite.frag) |
| Non-photorealistic rendering | `shade` quantizes diffuse lighting. `toonOutline` detects depth and normal edges. [lighting.frag](shaders/lighting.frag), [composite.frag](shaders/composite.frag) |
| Fast approximate anti-aliasing (FXAA) | `filterFXAA` smooths luminance edges in the composed image before ImGui is drawn. [present.frag](shaders/present.frag), [Frame.cpp](src/Frame.cpp) |
| Area lights | `areaContribution` integrates four samples from a downward-facing rectangular emitter above the scene. [lighting.frag](shaders/lighting.frag), [Application.cpp](src/Application.cpp) |
| Volumetric lighting | `scatterPointLight` integrates light and shadow along the camera ray through the point light's volume. [composite.frag](shaders/composite.frag) |

## TODO

### Hybrid rendering (ray tracing and rasterization)

- [ ] **Ray-traced shadows**
- [ ] **Ray-traced ambient occlusion**
- [ ] **Ray-traced reflections**

## Trace a frame

| Topic | Read |
| --- | --- |
| Instance, device, staging uploads | [VulkanContext.cpp](src/VulkanContext.cpp) |
| glTF vertices, indices, and material textures | [Scene.cpp](src/Scene.cpp) |
| Swapchain, descriptors, and G-buffer images | [Application.cpp](src/Application.cpp) |
| Acquire, shadow, geometry, lighting, composition, and presentation passes | [Frame.cpp](src/Frame.cpp) |
| Dynamic rendering pipeline setup | [Pipeline.cpp](src/Pipeline.cpp) |

The renderer supports two frames in flight. It waits on each slot's fence before reusing the slot's command buffer, acquire semaphore, uniform buffer, descriptor set, and render targets. Each swapchain image has its own presentation semaphore.

The geometry pass writes base color and metallic, world-space normals and roughness, and depth. The lighting pass reconstructs the world-space position from depth and shades a fullscreen triangle with diffuse and specular lighting.

Try a directional shadow map. On supported hardware, you can add ray-traced shadows to the existing lighting pass. [Khronos Sponza ray-query sample](https://docs.vulkan.org/samples/latest/samples/extensions/ray_queries/README.html)
