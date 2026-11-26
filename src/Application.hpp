#pragma once

#include "Scene.hpp"
#include "VulkanContext.hpp"

#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstddef>
#include <deque>
#include <memory>

struct RunOptions
{
    std::filesystem::path scene;
    bool validate = false;
    uint32_t frameLimit = 0;
};

class Application
{
public:
    ~Application();
    void run(const RunOptions& options);

private:
    struct Window
    {
        GLFWwindow* handle = nullptr;
        bool initialized = false;
        ~Window();
    };

    struct SwapImage
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
    };

    struct FrameImages
    {
        Image baseMetallic;
        Image normalRoughness;
        Image depth;
        Image litColor;
        Image compositeColor;
    };

    struct Frame
    {
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkFence finished = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkDescriptorSet effectsSet = VK_NULL_HANDLE;
        Buffer uniforms;
        void* mappedUniforms = nullptr;
        Image shadowAtlas;
        std::unique_ptr<FrameImages> images;
    };

    struct Camera
    {
        glm::vec3 eye{};
        float yaw = 0.0f;
        float pitch = 0.0f;
        float speed = 1.0f;
        float farPlane = 100.0f;

        glm::vec3 forward() const;
    };

    struct GeometryPush
    {
        glm::mat4 viewProjection;
        glm::vec4 baseColor;
        glm::vec4 material;
    };

    enum class LightType : int
    {
        directional = 0,
        point = 1,
        area = 2,
    };

    struct Light
    {
        uint32_t id;
        LightType type;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        float extent = 1.0f;
        bool enabled = true;
    };

    enum class LightGizmoMode { move, rotate };

    struct alignas(16) LightUniform
    {
        glm::vec4 positionExtent;
        glm::vec4 colorIntensity;
        glm::vec4 direction;
        glm::vec4 axisX;
        glm::ivec4 type;
    };

    struct alignas(16) EffectsUniforms
    {
        glm::mat4 viewProjection;
        glm::mat4 inverseViewProjection;
        std::array<glm::mat4, 6> shadowViewProjection;
        glm::vec4 eyeSceneScale;
        int debugView;
        int shadowLightIndex;
        int enabledEffects;
        int lightCount;
        std::array<LightUniform, MAX_SCENE_LIGHTS> lights;
    };

    static_assert(sizeof(GeometryPush) == 96);
    static_assert(sizeof(LightUniform) == 80);
    static_assert(offsetof(EffectsUniforms, lights) == 544);

    enum Effect : int
    {
        ssao = 1 << 0,
        pointShadows = 1 << 1,
        reflections = 1 << 2,
        toon = 1 << 3,
        fxaa = 1 << 4,
        areaLighting = 1 << 5,
        volumetricLighting = 1 << 6,
    };

    static constexpr uint32_t shadowFaceSize = 512;
    static constexpr VkExtent2D shadowAtlasExtent{shadowFaceSize * 3, shadowFaceSize * 2};

    void initialize(const RunOptions& options);
    void createFrames();
    void initializeOverlay();
    void configureOverlay(VkFormat format);
    void shutdownOverlay();
    void buildOverlay();
    bool buildLightEditor();
    void drawLightViewport(bool editSelectedLight);
    void initializeLights(const Scene& scene);
    glm::vec3 lightPosition(const Light& light) const;
    glm::vec3 lightPlacementPosition() const;
    glm::mat4 cameraView() const;
    glm::mat4 cameraProjection() const;
    void createDescriptors(const Scene& scene);
    void createSwapchain();
    void destroySwapchain();
    bool recreateSwapchain();
    void uploadScene(const Scene& scene);
    void updateCamera(float deltaTime);
    bool drawFrame();
    void recordFrame(const Frame& frame, uint32_t imageIndex);
    EffectsUniforms makeEffectsUniforms(const glm::mat4& viewProjection) const;
    void recordShadowPass(const Frame& frame, const EffectsUniforms& uniforms);
    void drawScene(VkCommandBuffer commands, const glm::mat4& viewProjection);
    void recordGeometryPass(const Frame& frame, const glm::mat4& viewProjection);
    void recordScreenPass(const Frame& frame, const Image& target, VkPipeline pipeline);
    void recordPresentPass(const Frame& frame, uint32_t imageIndex);
    void drawFullscreen(const Frame& frame, VkImageView target, VkPipeline pipeline, bool drawOverlay);

    Window window;
    VulkanContext vk;
    Buffer vertices;
    Buffer indices;
    std::deque<Image> textures;
    std::vector<Primitive> primitives;
    std::vector<Material> materials;
    std::vector<VkDescriptorSet> materialSets;
    std::vector<SwapImage> swapImages;
    static constexpr size_t framesInFlight = 2;
    std::array<Frame, framesInFlight> frames;
    size_t frameIndex = 0;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkSampleCountFlagBits geometrySamples = VK_SAMPLE_COUNT_1_BIT;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout effectsSetLayout = VK_NULL_HANDLE;
    VkSampler materialSampler = VK_NULL_HANDLE;
    VkSampler attachmentSampler = VK_NULL_HANDLE;
    VkSampler screenSampler = VK_NULL_HANDLE;
    VkPipelineLayout geometryLayout = VK_NULL_HANDLE;
    VkPipelineLayout effectsLayout = VK_NULL_HANDLE;
    VkPipeline geometryPipeline = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    VkPipeline lightingPipeline = VK_NULL_HANDLE;
    VkPipeline compositePipeline = VK_NULL_HANDLE;
    VkPipeline presentPipeline = VK_NULL_HANDLE;

    VkFormat overlayFormat = VK_FORMAT_UNDEFINED;
    std::array<float, 120> fpsHistory{};
    int fpsSampleCount = 0;
    int nextFpsSample = 0;
    int fpsFrameCount = 0;
    float fpsSampleTime = 0.0f;

    Camera camera;
    std::vector<Light> lights;
    uint32_t nextLightId = 1;
    uint32_t selectedLightId = 0;
    uint32_t shadowLightId = 0;
    uint32_t cameraLightId = 0;
    LightGizmoMode lightGizmoMode = LightGizmoMode::move;
    bool showLightPositions = true;
    bool draggingLightGizmo = false;
    int effects = ssao | pointShadows | fxaa;
    glm::dvec2 previousCursor{};
    bool looking = false;
    bool resized = false;
    bool lightKeyWasDown = false;
    int debugView = 0;
};
