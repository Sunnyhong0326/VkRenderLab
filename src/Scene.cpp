#include "Scene.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
Scene makeEmptyScene()
{
    Scene scene;
    scene.textures = {
        {1, 1, true, {255, 255, 255, 255}},
        {1, 1, false, {255, 255, 255, 255}},
        {1, 1, false, {128, 128, 255, 255}},
    };
    scene.materials.emplace_back();
    return scene;
}

void calculateBounds(Scene& scene)
{
    if (scene.vertices.empty())
        throw std::runtime_error("The scene contains no supported mesh primitives.");

    glm::vec3 low = scene.vertices.front().position;
    glm::vec3 high = low;
    for (const auto& vertex : scene.vertices)
    {
        low = glm::min(low, vertex.position);
        high = glm::max(high, vertex.position);
    }
    scene.center = (low + high) * 0.5f;
    scene.radius = glm::max(glm::length(high - low) * 0.5f, 0.01f);
}

void addQuad(Scene& scene, const std::array<glm::vec3, 4>& corners,
             glm::vec3 normal, uint32_t material, float uvScale = 1.0f)
{
    const auto firstVertex = static_cast<uint32_t>(scene.vertices.size());
    const auto firstIndex = static_cast<uint32_t>(scene.indices.size());
    const std::array<glm::vec2, 4> uv{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    for (size_t i = 0; i < corners.size(); ++i)
        scene.vertices.push_back({corners[i], normal, uv[i] * uvScale});
    for (uint32_t index : {0, 1, 2, 2, 3, 0})
        scene.indices.push_back(firstVertex + index);
    scene.primitives.push_back({firstIndex, 6, material});
}

Texture readTexture(const cgltf_image& image, const std::filesystem::path& directory, bool srgb)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = nullptr;
    if (image.buffer_view)
    {
        const auto& view = *image.buffer_view;
        if (view.size > static_cast<size_t>(std::numeric_limits<int>::max()))
            throw std::runtime_error("The embedded texture exceeds the image decoder's input limit.");
        pixels = stbi_load_from_memory(cgltf_buffer_view_data(&view), static_cast<int>(view.size),
                                       &width, &height, &channels, 4);
    }
    else if (image.uri)
    {
        std::string uri(image.uri);
        if (uri.starts_with("data:"))
            throw std::runtime_error("Use external image files or a GLB with embedded image buffer views.");
        uri.resize(cgltf_decode_uri(uri.data()));
        const auto path = directory / std::filesystem::u8path(uri);
#ifdef _WIN32
        std::unique_ptr<FILE, decltype(&fclose)> file(_wfopen(path.c_str(), L"rb"), fclose);
        if (file)
            pixels = stbi_load_from_file(file.get(), &width, &height, &channels, 4);
#else
        pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
#endif
        if (!pixels)
            throw std::runtime_error("Could not decode texture: " + path.string());
    }
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> ownedPixels(pixels, stbi_image_free);
    if (!pixels)
        throw std::runtime_error("Could not decode the glTF image.");
    const auto size = static_cast<size_t>(width) * height * 4;
    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height), srgb,
            std::vector<uint8_t>(pixels, pixels + size)};
}

void addPrimitive(Scene& scene, const cgltf_primitive& source, const cgltf_data& gltf,
                  const glm::mat4& world, const glm::mat3& normalMatrix)
{
    if (source.type != cgltf_primitive_type_triangles ||
        (source.material && source.material->alpha_mode == cgltf_alpha_mode_blend))
        return;

    const auto* positions = cgltf_find_accessor(&source, cgltf_attribute_type_position, 0);
    const auto* normals = cgltf_find_accessor(&source, cgltf_attribute_type_normal, 0);
    const auto* uv = cgltf_find_accessor(&source, cgltf_attribute_type_texcoord, 0);
    if (!positions || positions->count == 0)
        return;

    const auto firstVertex = static_cast<uint32_t>(scene.vertices.size());
    const auto firstIndex = static_cast<uint32_t>(scene.indices.size());
    for (size_t i = 0; i < positions->count; ++i)
    {
        Vertex vertex{};
        cgltf_accessor_read_float(positions, i, &vertex.position.x, 3);
        if (normals)
            cgltf_accessor_read_float(normals, i, &vertex.normal.x, 3);
        if (uv)
            cgltf_accessor_read_float(uv, i, &vertex.uv.x, 2);
        vertex.position = glm::vec3(world * glm::vec4(vertex.position, 1.0f));
        if (normals && glm::dot(vertex.normal, vertex.normal) > 0.0f)
            vertex.normal = glm::normalize(normalMatrix * vertex.normal);
        scene.vertices.push_back(vertex);
    }

    const size_t indexCount = source.indices ? source.indices->count : positions->count;
    for (size_t i = 0; i < indexCount; ++i)
    {
        const size_t index = source.indices ? cgltf_accessor_read_index(source.indices, i) : i;
        if (index >= positions->count)
            throw std::runtime_error("A glTF index refers outside its vertex data.");
        scene.indices.push_back(firstVertex + static_cast<uint32_t>(index));
    }
    if (!normals)
    {
        for (size_t i = firstIndex; i + 2 < scene.indices.size(); i += 3)
        {
            auto& a = scene.vertices[scene.indices[i]];
            auto& b = scene.vertices[scene.indices[i + 1]];
            auto& c = scene.vertices[scene.indices[i + 2]];
            const glm::vec3 normal = glm::cross(b.position - a.position, c.position - a.position);
            a.normal += normal;
            b.normal += normal;
            c.normal += normal;
        }
        for (size_t i = firstVertex; i < scene.vertices.size(); ++i)
        {
            auto& normal = scene.vertices[i].normal;
            normal = glm::dot(normal, normal) > 0.0f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
        }
    }
    const uint32_t material = source.material
        ? static_cast<uint32_t>(source.material - gltf.materials + 1) : 0;
    scene.primitives.push_back({firstIndex, static_cast<uint32_t>(indexCount), material});
}

void addNode(Scene& scene, const cgltf_node& node, const cgltf_data& gltf)
{
    if (node.mesh)
    {
        glm::mat4 world(1.0f);
        cgltf_node_transform_world(&node, &world[0].x);
        const glm::mat3 linear(world);
        if (glm::abs(glm::determinant(linear)) > 1e-10f)
        {
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(linear));
            for (size_t i = 0; i < node.mesh->primitives_count; ++i)
                addPrimitive(scene, node.mesh->primitives[i], gltf, world, normalMatrix);
        }
    }
    for (size_t i = 0; i < node.children_count; ++i)
        addNode(scene, *node.children[i], gltf);
}
}

Scene makePracticeScene()
{
    Scene scene = makeEmptyScene();
    const uint32_t checker = static_cast<uint32_t>(scene.textures.size());
    scene.textures.push_back({2, 2, true, {
        226, 156, 70, 255, 65, 104, 154, 255,
        65, 104, 154, 255, 226, 156, 70, 255,
    }});
    scene.materials[0].baseTexture = checker;
    Material floor;
    floor.baseColor = glm::vec4(0.4f, 0.43f, 0.48f, 1.0f);
    scene.materials.push_back(floor);
    Material card;
    card.parameters.z = 0.5f;
    card.baseTexture = static_cast<uint32_t>(scene.textures.size());
    Texture cutout{8, 8, true, {}};
    Texture normalMap{8, 8, false, {}};
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            const bool inside = (2 * x - 7) * (2 * x - 7) + (2 * y - 7) * (2 * y - 7) < 45;
            cutout.rgba.insert(cutout.rgba.end(), {59, 197, 111, static_cast<uint8_t>(inside ? 255 : 0)});
            normalMap.rgba.insert(normalMap.rgba.end(), {
                static_cast<uint8_t>(128 + (2 * x - 7) * 12), 128, 225, 255,
            });
        }
    }
    scene.textures.push_back(std::move(cutout));
    card.normalTexture = static_cast<uint32_t>(scene.textures.size());
    scene.textures.push_back(std::move(normalMap));
    scene.materials.push_back(card);
    addQuad(scene, {{{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}}, {0, 0, 1}, 0, 3);
    addQuad(scene, {{{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}}}, {0, 0, -1}, 0, 3);
    addQuad(scene, {{{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}}}, {1, 0, 0}, 0, 3);
    addQuad(scene, {{{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}}}, {-1, 0, 0}, 0, 3);
    addQuad(scene, {{{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}}}, {0, 1, 0}, 0, 3);
    addQuad(scene, {{{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}}}, {0, -1, 0}, 0, 3);
    addQuad(scene, {{{-5, -1.05f, 5}, {5, -1.05f, 5}, {5, -1.05f, -5}, {-5, -1.05f, -5}}},
            {0, 1, 0}, 1);
    addQuad(scene, {{{-2.8f, -0.7f, 1}, {-1.3f, -0.7f, 1}, {-1.3f, 0.8f, 1}, {-2.8f, 0.8f, 1}}},
            {0, 0, 1}, 2);
    calculateBounds(scene);
    return scene;
}

Scene loadGltf(const std::filesystem::path& path)
{
    cgltf_options options{};
    cgltf_data* parsed = nullptr;
    const std::string filename = path.string();
    if (cgltf_parse_file(&options, filename.c_str(), &parsed) != cgltf_result_success)
        throw std::runtime_error("Could not open glTF: " + filename);

    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(parsed, cgltf_free);
    if (cgltf_load_buffers(&options, data.get(), filename.c_str()) != cgltf_result_success ||
        cgltf_validate(data.get()) != cgltf_result_success)
        throw std::runtime_error("Could not load or validate glTF buffers: " + filename);

    Scene scene = makeEmptyScene();
    std::map<std::pair<const cgltf_image*, bool>, uint32_t> textures;
    const auto textureIndex = [&](const cgltf_texture_view& view, bool srgb, uint32_t fallback)
    {
        if (!view.texture || !view.texture->image || view.texcoord != 0 ||
            (view.has_transform && view.transform.has_texcoord && view.transform.texcoord != 0))
            return fallback;
        const auto* image = view.texture->image;
        const auto key = std::pair(image, srgb);
        if (const auto found = textures.find(key); found != textures.end())
            return found->second;
        const auto index = static_cast<uint32_t>(scene.textures.size());
        scene.textures.push_back(readTexture(*image, path.parent_path(), srgb));
        textures.emplace(key, index);
        return index;
    };
    for (size_t i = 0; i < data->materials_count; ++i)
    {
        const auto& source = data->materials[i];
        Material material;
        material.parameters.x = 1.0f;
        if (source.has_pbr_metallic_roughness)
        {
            const auto& pbr = source.pbr_metallic_roughness;
            const float* color = pbr.base_color_factor;
            material.baseColor = {color[0], color[1], color[2], color[3]};
            material.parameters.x = pbr.metallic_factor;
            material.parameters.y = pbr.roughness_factor;
            material.baseTexture = textureIndex(pbr.base_color_texture, true, 0);
            material.surfaceTexture = textureIndex(pbr.metallic_roughness_texture, false, 1);
        }
        if (source.alpha_mode == cgltf_alpha_mode_mask)
            material.parameters.z = source.alpha_cutoff;
        material.parameters.w = source.normal_texture.scale;
        material.normalTexture = textureIndex(source.normal_texture, false, 2);
        scene.materials.push_back(material);
    }

    const auto* activeScene = data->scene ? data->scene : data->scenes;
    if (!activeScene)
        throw std::runtime_error("The glTF has no scene.");
    for (size_t i = 0; i < activeScene->nodes_count; ++i)
        addNode(scene, *activeScene->nodes[i], *data);
    calculateBounds(scene);
    return scene;
}
