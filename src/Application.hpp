#pragma once

#include "Scene.hpp"
#include "VulkanContext.hpp"

#include <array>
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

    struct GBuffer
    {
        Image baseMetallic;
        Image normalRoughness;
        Image depth;
    };

    struct Frame
    {
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkFence finished = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkDescriptorSet lightingSet = VK_NULL_HANDLE;
        std::unique_ptr<GBuffer> gbuffer;
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

    struct LightingPush
    {
        glm::mat4 inverseViewProjection;
        glm::vec4 eye;
        glm::vec4 pointLight;
        glm::vec4 settings;
    };

    static_assert(sizeof(GeometryPush) == 96);
    static_assert(sizeof(LightingPush) == 112);

    void initialize(const RunOptions& options);
    void createFrames();
    void initializeOverlay();
    void configureOverlay(VkFormat format);
    void shutdownOverlay();
    void buildOverlay();
    void createDescriptors(const Scene& scene);
    void createSwapchain();
    void destroySwapchain();
    bool recreateSwapchain();
    void uploadScene(const Scene& scene);
    void updateCamera(float deltaTime);
    bool drawFrame();
    void recordFrame(const Frame& frame, uint32_t imageIndex);
    void recordGeometryPass(const Frame& frame, const glm::mat4& viewProjection);
    void recordLightingPass(const Frame& frame, uint32_t imageIndex, const glm::mat4& inverseViewProjection);

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
    VkDescriptorSetLayout lightingSetLayout = VK_NULL_HANDLE;
    VkSampler materialSampler = VK_NULL_HANDLE;
    VkSampler attachmentSampler = VK_NULL_HANDLE;
    VkPipelineLayout geometryLayout = VK_NULL_HANDLE;
    VkPipelineLayout lightingLayout = VK_NULL_HANDLE;
    VkPipeline geometryPipeline = VK_NULL_HANDLE;
    VkPipeline lightingPipeline = VK_NULL_HANDLE;

    VkFormat overlayFormat = VK_FORMAT_UNDEFINED;
    std::array<float, 120> fpsHistory{};
    int fpsSampleCount = 0;
    int nextFpsSample = 0;
    int fpsFrameCount = 0;
    float fpsSampleTime = 0.0f;

    Camera camera;
    glm::dvec2 previousCursor{};
    bool looking = false;
    bool resized = false;
    bool lightEnabled = true;
    bool lightKeyWasDown = false;
    int debugView = 0;
};
