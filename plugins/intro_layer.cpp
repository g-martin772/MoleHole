#include <imgui.h>

import GPP;
import MoleHole;
import vulkan;
import glm;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    struct IntroPlanetPushConstants
    {
        float Time = 0.0f;
        float Alpha = 0.0f;
        float LightIntensity = 0.0f;
        float ResolutionX = 0.0f;
        float ResolutionY = 0.0f;
    };

    constexpr const char* kTitleText = "MOLEHOLE";
    constexpr int kMoleLetters = 4;

    struct IntroLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, Renderer, IFileSystem, EventDispatcher, InputState, UiState>;

        IntroLayer(const std::shared_ptr<Logger>& logger,
                   const std::shared_ptr<Renderer>& renderer,
                   const std::shared_ptr<IFileSystem>& fileSystem,
                   const std::shared_ptr<EventDispatcher>& dispatcher,
                   const std::shared_ptr<InputState>& input,
                   const std::shared_ptr<UiState>& uiState)
            : HotReloadableLayer(logger), m_Renderer(renderer), m_FileSystem(fileSystem),
              m_Dispatcher(dispatcher), m_Input(input), m_UiState(uiState)
        {
        }

        void OnAttach() override
        {
            EnsureTitleFont();
        }

        void OnUpdate(float deltaTime) override
        {
            if (!m_UiState->IntroActive) return;

            if (m_Input->IsKeyDown(KeyCode::Escape) || m_Input->IsKeyDown(KeyCode::Space))
            {
                m_Timeline.Skip();
            }
            else
            {
                m_Timeline.Update(deltaTime);
            }

            if (!m_Timeline.IsActive())
            {
                m_UiState->IntroActive = false;
            }
        }

        void OnRenderGraph(RenderGraph& graph) override
        {
            if (!m_UiState->IntroActive) return;

            const auto colorTarget = graph.GetPrimaryColorTarget();
            if (colorTarget == kInvalidRenderGraphHandle) return;
            const auto extent = graph.GetImageExtent(colorTarget);
            if (extent.width == 0 || extent.height == 0) return;

            EnsurePlanetPipeline(graph.GetImageFormat(colorTarget));
            if (!m_PlanetPipeline) return;

            IntroPlanetPushConstants push{};
            push.Time = m_Timeline.Time();
            push.Alpha = m_Timeline.Alpha();
            push.LightIntensity = m_Timeline.LightIntensity();
            push.ResolutionX = static_cast<float>(extent.width);
            push.ResolutionY = static_cast<float>(extent.height);

            graph.AddGraphicsPass(
                "IntroLayer.Planet", {}, {},
                {RenderGraphAttachment{
                    .Handle = colorTarget, .LoadOp = vk::AttachmentLoadOp::eClear,
                    .Clear = vk::ClearValue(vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f))
                }},
                std::nullopt,
                [this, push](const vk::CommandBuffer cmd, RenderGraph&)
                {
                    const auto pipeline = m_PlanetPipeline->GetPipeline();
                    if (!pipeline) return;
                    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->GetPipeline());
                    cmd.pushConstants(pipeline->GetLayout(), vk::ShaderStageFlagBits::eFragment, 0,
                                      sizeof(push), &push);
                    cmd.draw(3, 1, 0, 0);
                });
        }

        void OnUiRender() override
        {
            if (!m_UiState->IntroActive) return;

            const ImGuiIO& io = ImGui::GetIO();
            const float windowWidth = io.DisplaySize.x;
            const float windowHeight = io.DisplaySize.y;
            if (windowWidth <= 0.0f || windowHeight <= 0.0f) return;

            m_Renderer->ResizeBufferTarget(
                m_LayerTarget.Id,
                glm::uvec2{static_cast<std::uint32_t>(windowWidth), static_cast<std::uint32_t>(windowHeight)});

            ImDrawList* const drawList = ImGui::GetForegroundDrawList();

            drawList->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(windowWidth, windowHeight), IM_COL32_BLACK);

            if (const auto target = m_Renderer->GetRenderTargetInfo(m_LayerTarget.Id))
            {
                drawList->AddImage(reinterpret_cast<ImTextureID>(target->ImGuiTexture),
                                   ImVec2(0.0f, 0.0f), ImVec2(windowWidth, windowHeight));
            }

            if (m_Timeline.VisibleLetterCount() <= 0) return;

            ImFont* const font = m_TitleFont ? m_TitleFont : io.FontDefault;
            ImGui::PushFont(font);

            const int charsToShow = std::min(m_Timeline.VisibleLetterCount(), IntroTimeline::TotalLetters);
            const ImVec2 fullTextSize = ImGui::CalcTextSize(kTitleText);
            const float textX = (windowWidth - fullTextSize.x) * 0.5f;
            const float textY = (windowHeight - fullTextSize.y) * 0.5f;

            float globalAlpha = static_cast<float>(charsToShow) / static_cast<float>(IntroTimeline::TotalLetters);
            globalAlpha = globalAlpha * globalAlpha * (3.0f - 2.0f * globalAlpha); // smoothstep

            float currentX = textX;
            for (int i = 0; i < charsToShow; ++i)
            {
                const char singleChar[2] = {kTitleText[i], '\0'};
                const ImVec2 charSize = ImGui::CalcTextSize(singleChar);
                const bool isMole = i < kMoleLetters;
                const ImVec4 charColorVec = isMole ? ImVec4(1.0f, 1.0f, 1.0f, globalAlpha)
                                                    : ImVec4(0.0f, 0.0f, 0.0f, globalAlpha);
                const ImU32 charColor = ImGui::ColorConvertFloat4ToU32(charColorVec);

                const float glowAlpha = 0.3f * globalAlpha;
                for (int j = 1; j <= 3; ++j)
                {
                    const ImU32 glowColor = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(0.7f, 0.9f, 1.0f, glowAlpha / static_cast<float>(j)));
                    drawList->AddText(font, font->LegacySize,
                                      ImVec2(currentX + static_cast<float>(j) * 2.0f, textY + static_cast<float>(j) * 2.0f),
                                      glowColor, singleChar);
                }

                drawList->AddText(font, font->LegacySize, ImVec2(currentX, textY), charColor, singleChar);
                currentX += charSize.x;
            }

            ImGui::PopFont();
        }

    private:
        void EnsureTitleFont()
        {
            if (m_TitleFont) return;
            m_TitleFont = ImGui::GetIO().Fonts->AddFontFromFileTTF("font/DidotLTPro-Bold.ttf", 120.0f);
            if (!m_TitleFont)
            {
                m_Logger->Warn("IntroLayer: failed to load title font, falling back to default");
            }
        }

        void EnsurePlanetPipeline(vk::Format colorFormat)
        {
            if (m_PlanetPipeline && m_PlanetColorFormat == colorFormat) return;
            const auto device = m_Renderer->GetDevice();
            m_PlanetPipeline = std::make_shared<ShaderPipeline>(
                device,
                VulkanPipelineSpecification{
                    .colorFormat = colorFormat,
                    .depthFormat = vk::Format::eUndefined,
                    .enableBlending = false,
                    .cullMode = vk::CullModeFlagBits::eNone
                },
                ShaderPipelineDescription{
                    .vertex = ShaderSource{
                        .path = m_FileSystem->ResolveAssetPath("shaders", "intro_planet.vert"),
                        .stage = ShaderStage::Vertex
                    },
                    .fragment = ShaderSource{
                        .path = m_FileSystem->ResolveAssetPath("shaders", "intro_planet.frag"),
                        .stage = ShaderStage::Fragment
                    },
                    .enableHotReload = true
                },
                m_FileSystem, m_Dispatcher, m_Logger);
            if (!m_PlanetPipeline->StartOnRenderThread())
            {
                m_Logger->Error("IntroLayer: failed to compile intro planet shader");
            }
            m_PlanetColorFormat = colorFormat;
        }

        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<EventDispatcher> m_Dispatcher;
        std::shared_ptr<InputState> m_Input;
        std::shared_ptr<UiState> m_UiState;

        IntroTimeline m_Timeline;
        ImFont* m_TitleFont = nullptr;

        std::shared_ptr<ShaderPipeline> m_PlanetPipeline;
        vk::Format m_PlanetColorFormat = vk::Format::eUndefined;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(IntroLayer)
