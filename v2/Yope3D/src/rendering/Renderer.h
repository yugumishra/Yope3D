#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <array>

class GpuDevice;
class Window;
class Swapchain;
class RenderPass;

// ---------------------------------------------------------------------------
// Vertex
//
// position: NDC xyz; z is unused until transformation matrices are added.
// color:    RGB vertex color, linearly interpolated by the rasteriser.
// ---------------------------------------------------------------------------

struct Vertex {
    float position[3];
    float color[3];
};

// ---------------------------------------------------------------------------
// Renderer
//
// Owns everything between the logical device and the screen:
//   - Swapchain + image views
//   - Render pass
//   - Graphics pipeline
//   - Framebuffers (one per swapchain image, rebuilt on resize)
//   - Command pool + per-frame command buffers
//   - Synchronisation objects (2 frames in flight)
//   - Active vertex + index buffers
//
// Resize handling: Window sets a resized flag when the framebuffer changes.
// drawFrame() checks that flag and VK_ERROR_OUT_OF_DATE_KHR return codes,
// calling recreateSwapchain() which only rebuilds the swapchain-dependent
// objects (swapchain, image views, framebuffers).  Pipeline and render pass
// survive resize unchanged.
// ---------------------------------------------------------------------------

class Renderer {
public:
    Renderer(GpuDevice& gpu, Window& window);
    ~Renderer();

    // Upload a new mesh.  Destroys previous buffers.  Safe before render loop.
    void setMesh(GpuDevice& gpu,
                 const std::vector<Vertex>&   vertices,
                 const std::vector<uint32_t>& indices);

    void drawFrame(GpuDevice& gpu, Window& window);
    void waitIdle(GpuDevice& gpu);

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

private:
    static constexpr int MAX_FRAMES = 2;

    std::unique_ptr<Swapchain>  swapchain;
    std::unique_ptr<RenderPass> renderPass;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       pipeline       = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> framebuffers;

    VkCommandPool                           commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, MAX_FRAMES> cmdBuffers{};

    std::array<VkSemaphore, MAX_FRAMES> imageAvailable{};
    std::vector<VkSemaphore>            renderFinished;  // one per swapchain image, not per frame
    std::array<VkFence,     MAX_FRAMES> inFlightFence{};
    uint32_t currentFrame = 0;

    VkBuffer       vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer       indexBuffer  = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory  = VK_NULL_HANDLE;
    uint32_t       indexCount   = 0;

    void createRenderPass(GpuDevice& gpu);
    void createPipeline(VkDevice device);
    void createFramebuffers(VkDevice device);
    void createCommandPool(GpuDevice& gpu);
    void allocateCommandBuffers(VkDevice device);
    void createSyncObjects(VkDevice device);
    void setDefaultMesh(GpuDevice& gpu);

    void recreateSwapchain(GpuDevice& gpu, Window& window);
    void destroyFramebuffers(VkDevice device);
    void destroyMesh(VkDevice device);

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);

    void createBuffer(GpuDevice& gpu, VkDeviceSize size,
                      VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                      VkBuffer& buf, VkDeviceMemory& mem);

    void uploadViaStaging(GpuDevice& gpu, const void* data, VkDeviceSize size,
                          VkBufferUsageFlags dstUsage,
                          VkBuffer& outBuffer, VkDeviceMemory& outMemory);
    
    void copyBuffer(GpuDevice& gpu, VkBuffer src, VkBuffer dst, VkDeviceSize size);

    VkCommandBuffer beginOneTimeCommands(VkDevice device);
    void            endOneTimeCommands(VkDevice device, VkQueue queue, VkCommandBuffer cmd);

    static uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t filter, VkMemoryPropertyFlags props);
};
