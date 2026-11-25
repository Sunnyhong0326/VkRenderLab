#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <vector>

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct Texture
{
    uint32_t width;
    uint32_t height;
    bool srgb;
    std::vector<uint8_t> rgba;
};

struct Material
{
    glm::vec4 baseColor{1.0f};
    glm::vec4 parameters{0.0f, 1.0f, -1.0f, 1.0f}; // Metallic, roughness, cutoff, normal scale.
    uint32_t baseTexture = 0;
    uint32_t surfaceTexture = 1;
    uint32_t normalTexture = 2;
};

struct Primitive
{
    uint32_t firstIndex;
    uint32_t indexCount;
    uint32_t material;
};

struct Scene
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<Primitive> primitives;
    glm::vec3 center{};
    float radius = 1.0f;
};

Scene makePracticeScene();
Scene loadGltf(const std::filesystem::path& path);
