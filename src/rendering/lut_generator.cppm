export module MoleHole:Rendering.Lut;

import std;
import glm;
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

    // Loads an equirectangular HDR skybox
    [[nodiscard]] std::shared_ptr<GPP::VulkanImage> LoadSkyboxTexture(
        const std::shared_ptr<GPP::VulkanDevice>& device, GPP::VulkanCommandPool& uploadPool, vk::Queue queue,
        const std::shared_ptr<GPP::IFileSystem>& fileSystem, const std::filesystem::path& relativePath,
        const std::shared_ptr<GPP::Logger>& logger);

    // A 1x1 solid-color texture, used as a descriptor-valid stand-in wherever an optional texture
    // (e.g. a glTF material with no base color texture) has nothing real to bind.
    [[nodiscard]] std::shared_ptr<GPP::VulkanImage> GenerateSolidColorTexture(
        const std::shared_ptr<GPP::VulkanDevice>& device, GPP::VulkanCommandPool& uploadPool, vk::Queue queue,
        const glm::vec4& color, const std::shared_ptr<GPP::Logger>& logger);
}
