cmake_minimum_required(VERSION 3.20)
project(VkRenderLab LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_path(CGLTF_INCLUDE_DIR cgltf.h REQUIRED)
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)

set(SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
foreach(shader IN ITEMS geometry.vert geometry.frag shadow.vert shadow.frag lighting.vert
                        lighting.frag lighting_ms.frag composite.frag composite_ms.frag present.frag)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}")
    set(defines)
    if(shader MATCHES "^(.+)_ms[.]frag$")
        set(source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${CMAKE_MATCH_1}.frag")
        set(defines -DMULTISAMPLED)
    endif()
    set(shader_dependencies "${source}")
    if(shader MATCHES "^(lighting(_ms)?|composite(_ms)?|present)[.]frag$")
        list(APPEND shader_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/shaders/effects.glsl")
    endif()
    set(output "${SHADER_DIR}/${shader}.spv")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_DIR}"
        COMMAND "${GLSLC}" --target-env=vulkan1.3 ${defines} "${source}" -o "${output}"
        DEPENDS ${shader_dependencies}
        VERBATIM
    )
    list(APPEND SHADER_OUTPUTS "${output}")
endforeach()
add_custom_target(compile_shaders DEPENDS ${SHADER_OUTPUTS})

add_executable(vk_render_lab
    src/main.cpp
    src/Application.cpp
    src/Frame.cpp
    src/Overlay.cpp
    src/Pipeline.cpp
    src/Scene.cpp
    src/VulkanContext.cpp
)
add_dependencies(vk_render_lab compile_shaders)
target_compile_features(vk_render_lab PRIVATE cxx_std_20)
target_compile_definitions(vk_render_lab PRIVATE
    GLFW_INCLUDE_NONE GLM_FORCE_DEPTH_ZERO_TO_ONE VULKAN_SHADER_DIR="${SHADER_DIR}"
)
target_include_directories(vk_render_lab SYSTEM PRIVATE "${CGLTF_INCLUDE_DIR}" "${Stb_INCLUDE_DIR}")
target_link_libraries(vk_render_lab PRIVATE Vulkan::Vulkan glfw glm::glm imgui::imgui)
