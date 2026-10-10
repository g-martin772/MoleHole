export module MoleHole:Rendering.Latex;

import std;
import vulkan;
import GPP;
import imgui;

export namespace MoleHole
{
    class LatexRenderer final : public GPP::IService
    {
    public:
        using Dependencies = std::tuple<GPP::Logger>;

        explicit LatexRenderer(std::shared_ptr<GPP::Logger> logger);
        ~LatexRenderer() override;

        LatexRenderer(const LatexRenderer&) = delete;
        LatexRenderer& operator=(const LatexRenderer&) = delete;

        [[nodiscard]] bool IsAvailable() const noexcept { return m_Available; }

        [[nodiscard]] std::shared_ptr<GPP::VulkanImage> Render(
            const std::shared_ptr<GPP::VulkanDevice>& device, GPP::VulkanCommandPool& uploadPool, vk::Queue queue,
            const std::string& latex, int dpi = 200);

    private:
        [[nodiscard]] bool RenderToPng(const std::string& latex, const std::filesystem::path& pngPath,
                                       int dpi) const;
        [[nodiscard]] static std::string HashKey(const std::string& latex, int dpi);

        std::shared_ptr<GPP::Logger> m_Logger;
        std::filesystem::path m_TempDir;
        bool m_Available = false;
        std::unordered_map<std::string, std::shared_ptr<GPP::VulkanImage>> m_Cache;
    };

    class LatexFormulaView
    {
    public:
        void Draw(LatexRenderer& renderer, const std::shared_ptr<GPP::VulkanDevice>& device,
                  GPP::VulkanCommandPool& uploadPool, vk::Queue queue, const std::string& latex, int dpi = 150);

    private:
        struct Entry
        {
            std::shared_ptr<GPP::VulkanImage> Image;
            void* ImGuiTexture = nullptr;
        };
        std::unordered_map<std::string, Entry> m_Entries;
    };
}
