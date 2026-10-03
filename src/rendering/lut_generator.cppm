export module MoleHole:Rendering.Lut;

import std;
import vulkan;
import GPP;

export namespace MoleHole
{
    [[nodiscard]] std::shared_ptr<GPP::VulkanImage> GenerateBlackbodyLut(
        const std::shared_ptr<GPP::VulkanDevice>& device, GPP::VulkanCommandPool& uploadPool, vk::Queue queue,
        const std::shared_ptr<GPP::Logger>& logger);

    [[nodiscard]] std::shared_ptr<GPP::VulkanImage> GenerateAccelerationLut(
        const std::shared_ptr<GPP::VulkanDevice>& device, GPP::VulkanCommandPool& uploadPool, vk::Queue queue,
        const std::shared_ptr<GPP::Logger>& logger);

    [[nodiscard]] std::shared_ptr<GPP::VulkanImage> GenerateHrDiagramLut(
        const std::shared_ptr<GPP::VulkanDevice>& device, GPP::VulkanCommandPool& uploadPool, vk::Queue queue,
        const std::shared_ptr<GPP::Logger>& logger);

    // Loads an equirectangular HDR skybox (ported from BlackHoleRenderer::LoadSkybox).
    [[nodiscard]] std::shared_ptr<GPP::VulkanImage> LoadSkyboxTexture(
        const std::shared_ptr<GPP::VulkanDevice>& device, GPP::VulkanCommandPool& uploadPool, vk::Queue queue,
        const std::shared_ptr<GPP::IFileSystem>& fileSystem, const std::filesystem::path& relativePath,
        const std::shared_ptr<GPP::Logger>& logger);
}
