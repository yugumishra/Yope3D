#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

class GpuDevice;

// ---------------------------------------------------------------------------
// DynamicMeshBuffer — host-visible, persistently mapped vertex + index pair for
// a mesh whose geometry is rewritten from the CPU rather than uploaded once.
//
// Sibling of LineBuffer / UIBuffer / Text3DBuffer, and it obeys the same rule
// they do: one instance per frame-in-flight, and a slot may only be written
// after drawFrame's vkWaitForFences has retired that slot's submission. The
// difference is ownership — those three are per-Renderer singletons, whereas a
// dynamic mesh is a separate draw with its own bindings, so the ring lives on
// the RenderMesh (see RenderMesh::kFramesInFlight).
//
// Capacity is fixed at init(). write() clamps rather than reallocating: growing
// a live allocation would need its own deferred-destroy path for the old
// buffers, and every consumer so far knows its worst-case size up front. This
// mirrors LineBuffer::kMaxVertices.
//
// Deliberately untyped (void* + byte counts) so this header stays free of any
// vertex-layout include and the gpu/ layer keeps not depending on world/.
// ---------------------------------------------------------------------------

class DynamicMeshBuffer {
public:
    DynamicMeshBuffer() = default;
    DynamicMeshBuffer(DynamicMeshBuffer&&) noexcept;
    DynamicMeshBuffer& operator=(DynamicMeshBuffer&&) noexcept;
    DynamicMeshBuffer(const DynamicMeshBuffer&) = delete;
    DynamicMeshBuffer& operator=(const DynamicMeshBuffer&) = delete;
    ~DynamicMeshBuffer() = default;

    // vertexStride is the size of one vertex in bytes; capacity is fixed here.
    void init(GpuDevice& gpu, uint32_t maxVertices, uint32_t vertexStride,
              uint32_t maxIndices);
    void destroy(VkDevice device);

    // Overwrite from the start. Counts beyond the capacities fixed at init() are
    // clamped; the clamped index count is what indexCount() then reports, so a
    // truncated write draws less geometry rather than reading past the end.
    void write(const void* verts, uint32_t vertCount,
               const uint32_t* indices, uint32_t idxCount);

    VkBuffer vertexBuffer() const { return vertBuf_; }
    VkBuffer indexBuffer()  const { return idxBuf_;  }
    uint32_t indexCount()   const { return idxCount_; }
    bool     valid()        const { return vertBuf_ != VK_NULL_HANDLE; }

private:
    VkBuffer       vertBuf_ = VK_NULL_HANDLE;
    VkDeviceMemory vertMem_ = VK_NULL_HANDLE;
    VkBuffer       idxBuf_  = VK_NULL_HANDLE;
    VkDeviceMemory idxMem_  = VK_NULL_HANDLE;
    uint8_t*       mappedVerts_ = nullptr;
    uint32_t*      mappedIdx_   = nullptr;

    uint32_t maxVerts_    = 0;
    uint32_t maxIndices_  = 0;
    uint32_t vertexStride_ = 0;
    uint32_t idxCount_    = 0;   // indices actually written by the last write()

    static uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t filter,
                                   VkMemoryPropertyFlags props);
    static VkBuffer makeBuffer(VkDevice device, VkPhysicalDevice pd,
                               VkDeviceSize size, VkBufferUsageFlags usage,
                               VkDeviceMemory& outMem);
};
