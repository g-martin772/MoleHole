#include "VulkanInstance.h"

#include <GLFW/glfw3.h>

#include "spdlog/spdlog.h"

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData) {
    switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            spdlog::error("[Vulkan]: {} - {}", pCallbackData->pMessageIdName, pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            spdlog::warn("[Vulkan]: {} - {}", pCallbackData->pMessageIdName, pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            spdlog::info("[Vulkan]: {} - {}", pCallbackData->pMessageIdName, pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
            spdlog::trace("[Vulkan]: {} - {}", pCallbackData->pMessageIdName, pCallbackData->pMessage);
            break;
        default:
            spdlog::error("[Vulkan]: {} - {}", pCallbackData->pMessageIdName, pCallbackData->pMessage);
            break;
    }

    return VK_FALSE;
}

void VulkanInstance::Init() {
    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName = "MoleHole";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "MoleHole Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_4;

    vk::InstanceCreateInfo createInfo;
    createInfo.pApplicationInfo = &appInfo;

    const std::vector<vk::ExtensionProperties> extensions = vk::enumerateInstanceExtensionProperties();
    spdlog::trace("Available Vulkan extensions:");
    for (const auto &extension: extensions) {
        spdlog::trace("\t{}", extension.extensionName.begin());
    }

    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector requiredExtensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    requiredExtensions.push_back("VK_EXT_debug_utils");

    for (const char *ext : requiredExtensions) {
        const bool found = std::ranges::any_of(extensions, [ext](const vk::ExtensionProperties &p) {
            return strcmp(ext, p.extensionName) == 0;
        });
        if (!found)
            spdlog::error("Vulkan extension {} could not be loaded!", ext);
    }

    createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
    createInfo.ppEnabledExtensionNames = requiredExtensions.data();

    std::vector<vk::LayerProperties> layers = vk::enumerateInstanceLayerProperties();
    spdlog::trace("Available Vulkan layers:");
    for (const auto &layer: layers) {
        spdlog::trace("\t{}", layer.layerName.begin());
    }

    const std::vector requiredLayers = {
#ifdef _DEBUG
        "VK_LAYER_KHRONOS_validation"
#endif
    };

    for (const char *layerName: requiredLayers) {
        bool layerFound = false;

        for (const auto &layerProperties: layers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            spdlog::error("Vulkan layer {} could not be loaded!", layerName);
        }
    }

    createInfo.enabledLayerCount = requiredLayers.size();
    createInfo.ppEnabledLayerNames = requiredLayers.data();

    try {
        m_Instance = vk::createInstance(createInfo);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to create Vulkan instance: {}", err.what());
        exit(1); // ;)
    }
    spdlog::trace("Created vulkan instance: successful");

    m_DynamicInstanceDispatcher = vk::detail::DispatchLoaderDynamic(m_Instance, vkGetInstanceProcAddr);

#ifdef _DEBUG
    vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo;
    debugMessengerCreateInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
                                               | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
                                               | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo
                                               | vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose;
    debugMessengerCreateInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral
                                           | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
                                           | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    debugMessengerCreateInfo.pfnUserCallback = reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(debugCallback);
    debugMessengerCreateInfo.pUserData = nullptr; // Optional

    try {
        m_DebugMessenger = m_Instance.createDebugUtilsMessengerEXT(
            debugMessengerCreateInfo, nullptr, m_DynamicInstanceDispatcher);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to create debug messenger: {}", err.what());
        exit(1);
    }
    spdlog::trace("Created debug messenger: successful");
#endif
}

void VulkanInstance::Shutdown() {
#ifdef _DEBUG
    m_Instance.destroyDebugUtilsMessengerEXT(m_DebugMessenger, nullptr, m_DynamicInstanceDispatcher);
#endif
    m_Instance.destroy();
}
