module MoleHole;

import :Rendering.Latex;
import std;
import vulkan;
import GPP;
import imgui;

using namespace GPP;

namespace MoleHole
{
    namespace
    {
        std::shared_ptr<VulkanImage> UploadRgba8AsFloat(const std::shared_ptr<VulkanDevice>& device,
                                                         VulkanCommandPool& uploadPool, vk::Queue queue,
                                                         std::uint32_t width, std::uint32_t height,
                                                         const unsigned char* rgba8, const std::string& debugName,
                                                         const std::shared_ptr<Logger>& logger)
        {
            std::vector<float> rgba32f(static_cast<std::size_t>(width) * height * 4);
            for (std::size_t i = 0; i < rgba32f.size(); ++i)
            {
                rgba32f[i] = static_cast<float>(rgba8[i]) / 255.0f;
            }

            const auto extent = vk::Extent3D{width, height, 1};
            const auto byteSize = static_cast<vk::DeviceSize>(rgba32f.size() * sizeof(float));

            VulkanBuffer staging(device, MakeStagingBufferSpecification(byteSize), logger);
            staging.Upload(rgba32f.data(), byteSize);

            auto image = std::make_shared<VulkanImage>(
                device,
                VulkanImageSpecification{
                    .extent = extent,
                    .format = vk::Format::eR32G32B32A32Sfloat,
                    .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .createSampler = true,
                    .samplerFilter = vk::Filter::eLinear,
                    .samplerAddressMode = vk::SamplerAddressMode::eClampToEdge,
                    .debugName = debugName
                },
                logger);

            ImmediateSubmit(uploadPool, queue, [&](const vk::CommandBuffer cmd)
            {
                TransitionImageLayout(cmd, image->GetImage(), vk::Format::eR32G32B32A32Sfloat,
                                      vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
                CopyBufferToImage(cmd, staging.GetBuffer(), image->GetImage(), extent);
                TransitionImageLayout(cmd, image->GetImage(), vk::Format::eR32G32B32A32Sfloat,
                                      vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
            });
            return image;
        }

        bool ToolAvailable(const char* versionCommand)
        {
            return std::system((std::string(versionCommand) + " > /dev/null 2>&1").c_str()) == 0;
        }
    }

    LatexRenderer::LatexRenderer(std::shared_ptr<GPP::Logger> logger) : m_Logger(std::move(logger))
    {
        m_TempDir = std::filesystem::temp_directory_path() / "molehole_latex";
        std::filesystem::create_directories(m_TempDir);

        m_Available = ToolAvailable("latex --version") && ToolAvailable("dvipng --version");
        if (m_Available)
        {
            m_Logger->Info("LatexRenderer: latex + dvipng found, rendering enabled");
        }
        else
        {
            m_Logger->Warn("LatexRenderer: latex or dvipng not found on PATH; LaTeX rendering disabled");
        }
    }

    LatexRenderer::~LatexRenderer()
    {
        try
        {
            if (!m_TempDir.empty() && std::filesystem::exists(m_TempDir))
            {
                std::filesystem::remove_all(m_TempDir);
            }
        }
        catch (...)
        {
        }
    }

    std::string LatexRenderer::HashKey(const std::string& latex, const int dpi)
    {
        std::size_t h = std::hash<std::string>{}(latex);
        h ^= std::hash<int>{}(dpi) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return std::format("{:x}", h);
    }

    bool LatexRenderer::RenderToPng(const std::string& latex, const std::filesystem::path& pngPath,
                                    const int dpi) const
    {
        const auto hash = HashKey(latex, dpi);
        const auto texPath = m_TempDir / (hash + ".tex");
        const auto dviPath = m_TempDir / (hash + ".dvi");

        {
            std::ofstream ofs(texPath);
            if (!ofs)
            {
                m_Logger->Error("LatexRenderer: failed to write {}", texPath.string());
                return false;
            }
            ofs << "\\documentclass[preview,border=2pt]{standalone}\n"
                << "\\usepackage{amsmath}\n"
                << "\\usepackage{xcolor}\n"
                << "\\begin{document}\n"
                << "\\color{white}\n"
                << latex << "\n"
                << "\\end{document}\n";
        }

        const auto latexCmd = std::format("cd {} && latex -interaction=nonstopmode {}.tex > /dev/null 2>&1",
                                          m_TempDir.string(), hash);
        if (std::system(latexCmd.c_str()) != 0)
        {
            m_Logger->Error("LatexRenderer: latex compilation failed for '{}'", latex);
            return false;
        }

        const auto dvipngCmd = std::format(
            "dvipng -D {} --truecolor -bg Transparent -fg 'rgb 1.0 1.0 1.0' -o {} {} > /dev/null 2>&1", dpi,
            pngPath.string(), dviPath.string());
        if (std::system(dvipngCmd.c_str()) != 0)
        {
            m_Logger->Error("LatexRenderer: dvipng conversion failed for '{}'", latex);
            return false;
        }

        return std::filesystem::exists(pngPath);
    }

    std::shared_ptr<VulkanImage> LatexRenderer::Render(const std::shared_ptr<VulkanDevice>& device,
                                                       VulkanCommandPool& uploadPool, const vk::Queue queue,
                                                       const std::string& latex, const int dpi)
    {
        if (!m_Available) return nullptr;

        const auto key = HashKey(latex, dpi);
        if (const auto it = m_Cache.find(key); it != m_Cache.end()) return it->second;

        const auto pngPath = m_TempDir / (key + ".png");
        if (!RenderToPng(latex, pngPath, dpi))
        {
            m_Cache[key] = nullptr;
            return nullptr;
        }

        int width = 0, height = 0, channels = 0;
        unsigned char* data = stbi_load(pngPath.string().c_str(), &width, &height, &channels, 4);
        if (!data)
        {
            m_Logger->Error("LatexRenderer: failed to load rendered PNG {}", pngPath.string());
            m_Cache[key] = nullptr;
            return nullptr;
        }

        auto image = UploadRgba8AsFloat(device, uploadPool, queue, static_cast<std::uint32_t>(width),
                                        static_cast<std::uint32_t>(height), data, "Latex:" + key, m_Logger);
        stbi_image_free(data);

        m_Cache[key] = image;
        return image;
    }

    void LatexFormulaView::Draw(LatexRenderer& renderer, const std::shared_ptr<VulkanDevice>& device,
                                VulkanCommandPool& uploadPool, const vk::Queue queue, const std::string& latex,
                                const int dpi)
    {
        auto& entry = m_Entries[latex];
        if (!entry.Image)
        {
            entry.Image = renderer.Render(device, uploadPool, queue, latex, dpi);
        }

        if (entry.Image)
        {
            if (!entry.ImGuiTexture)
            {
                entry.ImGuiTexture = reinterpret_cast<void*>(ImGui_ImplVulkan_AddTexture(
                    entry.Image->GetSampler(), entry.Image->GetImageView(),
                    static_cast<VkImageLayout>(vk::ImageLayout::eShaderReadOnlyOptimal)));
            }
            const auto extent = entry.Image->GetSpecification().extent;
            ImGui::Image(reinterpret_cast<ImTextureID>(entry.ImGuiTexture),
                        ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height)));
        }
        else
        {
            ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f),
                               "LaTeX rendering unavailable (latex + dvipng not found on PATH).");
            ImGui::TextWrapped("%s", latex.c_str());
        }
    }
}
