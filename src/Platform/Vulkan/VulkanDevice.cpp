#include "VulkanDevice.h"

// Define storage for dynamic dispatcher
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <spdlog/spdlog.h>
#include "VulkanInstance.h"

static void AddQueueToCreateInfo(std::vector<vk::DeviceQueueCreateInfo> &queueInfos, uint32_t queueFamilyIndex,
                                 uint32_t *resultIndex) {
    for (uint32_t i = 0; i < queueInfos.size(); i++) {
        if (queueInfos[i].queueFamilyIndex == queueFamilyIndex) {
            *resultIndex = i;
            return;
        }
    }

    vk::DeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    static float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueInfos.push_back(queueCreateInfo);
    *resultIndex = queueInfos.size() - 1;
}

void VulkanDevice::Init(const DeviceRequirements &requirements, VulkanInstance *instance) {
    m_Instance = instance;
    m_Requirements = requirements;

    std::vector<vk::PhysicalDevice> devices = instance->GetInstance().enumeratePhysicalDevices();

    spdlog::info("Available Graphics Devices:");
    for (const auto &device: devices) {
        bool suitable = true;
        vk::PhysicalDeviceProperties properties = device.getProperties();
        vk::PhysicalDeviceFeatures features = device.getFeatures();
        std::vector<vk::QueueFamilyProperties> queueFamilyProperties = device.getQueueFamilyProperties();

        spdlog::info("\t{}", properties.deviceName.data());
        spdlog::trace("\t\t Driver Version: {}.{}.{}",
                 VK_VERSION_MAJOR(properties.driverVersion),
                 VK_VERSION_MINOR(properties.driverVersion),
                 VK_VERSION_PATCH(properties.driverVersion));
        spdlog::trace("\t\t Type: {}", vk::to_string(properties.deviceType));
        spdlog::trace("\t\t VendorId: {}", properties.vendorID);
        spdlog::trace("\t\t Max Viewports: {}", properties.limits.maxViewports);
        spdlog::trace("\t\t Max Framebuffer Size: {}, {}", properties.limits.maxFramebufferHeight,
                 properties.limits.maxFramebufferWidth);
        spdlog::trace("\t\t Max Descriptorsets: {}", properties.limits.maxBoundDescriptorSets);
        spdlog::trace("\t\t Max memory allocations: {}", properties.limits.maxMemoryAllocationCount);
        spdlog::trace("\t\t Max dynamic Storage Buffers: {}", properties.limits.maxDescriptorSetStorageBuffersDynamic);
        spdlog::trace("\t\t Max dynamic Uniform Buffers: {}", properties.limits.maxDescriptorSetUniformBuffersDynamic);
        spdlog::trace("\t\t Max Storage Buffers: {}", properties.limits.maxDescriptorSetStorageBuffers);
        spdlog::trace("\t\t Max Uniform Buffers: {}", properties.limits.maxDescriptorSetUniformBuffers);
        spdlog::trace("\t\t Max Sampler: {}", properties.limits.maxDescriptorSetSamplers);
        spdlog::trace("\t\t Max input attachments: {}", properties.limits.maxDescriptorSetInputAttachments);
        spdlog::trace("\t\t Max sampled images: {}", properties.limits.maxDescriptorSetSampledImages);
        spdlog::trace("\t\t Max draw index: {}", properties.limits.maxDrawIndexedIndexValue);

        spdlog::trace("\t\t Supported device features:");
        spdlog::trace("\t\t\t TessellationShader: {}", (features.tessellationShader ? "Supported" : "Not Supported"));
        spdlog::trace("\t\t\t GeometryShader: {}", (features.geometryShader ? "Supported" : "Not Supported"));
        spdlog::trace("\t\t\t MultiViewport: {}", (features.multiViewport ? "Supported" : "Not Supported"));
        spdlog::trace("\t\t\t SamplerAnisotropy: {}", (features.samplerAnisotropy ? "Supported" : "Not Supported"));
        spdlog::trace("\t\t\t FillModeNonSolid: {}", (features.fillModeNonSolid ? "Supported" : "Not Supported"));
        spdlog::trace("\t\t\t SparseBinding: {}", (features.sparseBinding ? "Supported" : "Not Supported"));

        VulkanQueueIndices queueIndices;

        spdlog::trace("\t\t Supported queues:");
        for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueFamilyProperties.size(); ++queueFamilyIndex) {
            const vk::QueueFamilyProperties &queueFamily = queueFamilyProperties[queueFamilyIndex];

            spdlog::trace("\t\t\t Queue Family Index: {}", queueFamilyIndex);
            spdlog::trace("\t\t\t\t Queue Count:: {}", queueFamily.queueCount);
            spdlog::trace("\t\t\t\t Queue Flags: {}", vk::to_string(queueFamily.queueFlags));

            if (requirements.Graphics && queueIndices.Graphics == -1 && queueFamily.queueFlags &
                vk::QueueFlagBits::eGraphics) {
                queueIndices.Graphics = queueFamilyIndex;
            }

            if (requirements.Compute && queueFamily.queueFlags & vk::QueueFlagBits::eCompute) {
                queueIndices.Compute = queueFamilyIndex;
            }

            if (requirements.Transfer && queueFamily.queueFlags & vk::QueueFlagBits::eTransfer) {
                queueIndices.Transfer = queueFamilyIndex;
            }

            if (requirements.Sparse && queueFamily.queueFlags & vk::QueueFlagBits::eSparseBinding) {
                queueIndices.Sparse = queueFamilyIndex;
            }

            if (requirements.Present && device.getSurfaceSupportKHR(queueFamilyIndex, requirements.Surface)) {
                queueIndices.Present = queueFamilyIndex;
            }
        }

        if (suitable && !m_PhysicalDevice) {
            m_PhysicalDevice = device;
            m_QueueIndices = queueIndices;
            break;
        }
    }

    if (!m_PhysicalDevice)
    {
        spdlog::error("No suitable Vulkan physical device found!");
        return;
    }
    spdlog::info("Picked {}", m_PhysicalDevice.getProperties().deviceName.data());

    if (requirements.Graphics && m_QueueIndices.Graphics == -1) { spdlog::error("Required Graphics queue not found!"); return; }
    if (requirements.Transfer && m_QueueIndices.Transfer == -1) { spdlog::error("Required Transfer queue not found!"); return; }
    if (requirements.Compute  && m_QueueIndices.Compute  == -1) { spdlog::error("Required Compute queue not found!");  return; }
    if (requirements.Sparse   && m_QueueIndices.Sparse   == -1) { spdlog::error("Required Sparse queue not found!");   return; }
    if (requirements.Present  && m_QueueIndices.Present  == -1) { spdlog::error("Required Present queue not found!");  return; }

    std::vector<vk::DeviceQueueCreateInfo> deviceQueueInfos;
    if (requirements.Graphics)
        AddQueueToCreateInfo(deviceQueueInfos, m_QueueIndices.Graphics, &m_GraphicsIndex);
    if (requirements.Transfer)
        AddQueueToCreateInfo(deviceQueueInfos, m_QueueIndices.Transfer, &m_TransferIndex);
    if (requirements.Compute)
        AddQueueToCreateInfo(deviceQueueInfos, m_QueueIndices.Compute, &m_ComputeIndex);
    if (requirements.Sparse)
        AddQueueToCreateInfo(deviceQueueInfos, m_QueueIndices.Sparse, &m_SparseIndex);
    if (requirements.Present)
        AddQueueToCreateInfo(deviceQueueInfos, m_QueueIndices.Present, &m_PresentIndex);

    spdlog::trace("Selected Queue indices:");
    spdlog::trace("\t Graphics: {} ({})", m_QueueIndices.Graphics, m_GraphicsIndex);
    spdlog::trace("\t Transfer: {} ({})", m_QueueIndices.Transfer, m_TransferIndex);
    spdlog::trace("\t Compute: {} ({})", m_QueueIndices.Compute, m_ComputeIndex);
    spdlog::trace("\t Sparse: {} ({})", m_QueueIndices.Sparse, m_SparseIndex);
    spdlog::trace("\t Present: {} ({})", m_QueueIndices.Present, m_PresentIndex);

    vk::DeviceCreateInfo deviceCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = deviceQueueInfos.size();
    deviceCreateInfo.pQueueCreateInfos = deviceQueueInfos.data();

    vk::PhysicalDeviceFeatures deviceFeatures{};
    if (m_PhysicalDevice.getFeatures().fillModeNonSolid) deviceFeatures.fillModeNonSolid = VK_TRUE;
    if (m_PhysicalDevice.getFeatures().samplerAnisotropy) deviceFeatures.samplerAnisotropy = VK_TRUE;
    if (m_PhysicalDevice.getFeatures().wideLines) deviceFeatures.wideLines = VK_TRUE;
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

    std::vector deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME
    };
    deviceCreateInfo.enabledExtensionCount = deviceExtensions.size();
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

    // Enable features using pNext chain
    vk::PhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures;
    bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;

    vk::PhysicalDeviceAccelerationStructureFeaturesKHR asFeatures;
    asFeatures.accelerationStructure = VK_TRUE;
    asFeatures.pNext = &bufferDeviceAddressFeatures;

    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures;
    rtFeatures.rayTracingPipeline = VK_TRUE;
    rtFeatures.pNext = &asFeatures;

    vk::PhysicalDeviceFeatures2 deviceFeatures2;
    if (m_PhysicalDevice.getFeatures().fillModeNonSolid) deviceFeatures2.features.fillModeNonSolid = VK_TRUE;
    if (m_PhysicalDevice.getFeatures().samplerAnisotropy) deviceFeatures2.features.samplerAnisotropy = VK_TRUE;
    if (m_PhysicalDevice.getFeatures().wideLines) deviceFeatures2.features.wideLines = VK_TRUE;
    // Enable shaderInt64 for buffer addresses if needed, but usually implied or separate
    deviceFeatures2.features.shaderInt64 = VK_TRUE; 
    
    deviceFeatures2.pNext = &rtFeatures;

    deviceCreateInfo.pEnabledFeatures = nullptr;
    deviceCreateInfo.pNext = &deviceFeatures2;

    try {
        m_Device = m_PhysicalDevice.createDevice(deviceCreateInfo);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to create logical device: {}", err.what());
        return;
    }
    spdlog::trace("Created logical device: successful");

    // Initialize Dynamic Dispatcher (Local)
    m_Dispatcher = vk::detail::DispatchLoaderDynamic(m_Instance->GetInstance(), vkGetInstanceProcAddr, m_Device);
    // Initialize Default Dispatcher (Global)
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_Device);

    for (auto &queueInfo: deviceQueueInfos) {
        vk::Queue queue = m_Device.getQueue(queueInfo.queueFamilyIndex, 0);
        m_Queues.push_back(queue);
    }

    m_SurfaceCapabilities = m_PhysicalDevice.getSurfaceCapabilitiesKHR(requirements.Surface);
    m_SurfaceFormats = m_PhysicalDevice.getSurfaceFormatsKHR(requirements.Surface);
    m_SurfacePresentModes = m_PhysicalDevice.getSurfacePresentModesKHR(requirements.Surface);

    std::vector depthFormats = {
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD32Sfloat,
        vk::Format::eD24UnormS8Uint,
        vk::Format::eD16UnormS8Uint,
        vk::Format::eD16Unorm
    };

    auto flags = vk::FormatFeatureFlagBits::eDepthStencilAttachment;
    for (auto &depthFormat: depthFormats) {
        auto props = m_PhysicalDevice.getFormatProperties(depthFormat);
        if ((props.linearTilingFeatures & flags) == flags) {
            m_DepthFormat = depthFormat;
            break;
        } else if ((props.optimalTilingFeatures & flags) == flags) {
            m_DepthFormat = depthFormat;
            break;
        }
    }
}

void VulkanDevice::Shutdown() {
    m_Device.destroy();
}

void VulkanDevice::WaitIdle() {
    m_Device.waitIdle();
}
