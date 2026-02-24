#pragma once

#include <vulkan/vulkan.hpp>

class VulkanInstance
{
public:
    void Init();
    void Shutdown();

    vk::Instance GetInstance() const { return m_Instance; }
private:
    vk::Instance m_Instance;
    vk::detail::DispatchLoaderDynamic m_DynamicInstanceDispatcher;
    vk::DebugUtilsMessengerEXT m_DebugMessenger;
};