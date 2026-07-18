#pragma once
#include <vulkan/vulkan.h>
#include <vector>

// ---------------------------------------------------------------------------
// VulkanInstance
//
// Owns the VkInstance and, in debug builds, a VkDebugUtilsMessengerEXT.
// Validation layers are enabled unconditionally in debug builds (NDEBUG not
// defined).  The debug messenger is chained into VkInstanceCreateInfo::pNext
// so messages during vkCreateInstance / vkDestroyInstance are also captured.
// ---------------------------------------------------------------------------

class VulkanInstance {
public:
    VulkanInstance();
    ~VulkanInstance();

    VkInstance get() const { return instance; }

    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

#ifdef NDEBUG
    static constexpr bool validationEnabled = false;
#else
    static constexpr bool validationEnabled = true;
#endif

    static const std::vector<const char*> validationLayers;

private:
    VkInstance               instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

    void createInstance();
    void setupDebugMessenger();
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& info);

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT        type,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* userData);

    // vkCreate/DestroyDebugUtilsMessengerEXT are not in the static dispatch
    // table — they must be resolved at runtime via vkGetInstanceProcAddr.
    static VkResult createDebugMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDebugUtilsMessengerEXT* pMessenger);

    static void destroyDebugMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT messenger,
        const VkAllocationCallbacks* pAllocator);
};
