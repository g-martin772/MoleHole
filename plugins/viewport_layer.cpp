import GPP;
import vulkan;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;

namespace
{
    // Phase 1 placeholder: clears the offscreen viewport buffer and docks it into the main
    // window. The real raytrace -> bloom -> composite -> overlay pass graph lands here in
    // Phase 3.
    struct ViewportLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, Renderer>;

        ViewportLayer(const std::shared_ptr<Logger>& logger, const std::shared_ptr<Renderer>& renderer)
            : HotReloadableLayer(logger), m_Renderer(renderer)
        {
        }

        void OnAttach() override
        {
            m_Logger->Info("ViewportLayer attached");
        }

        void OnDetach() override
        {
            m_Logger->Info("ViewportLayer detached");
        }

        void OnRenderGraph(RenderGraph& graph) override
        {
            const auto colorTarget = graph.GetPrimaryColorTarget();
            if (colorTarget == kInvalidRenderGraphHandle)
            {
                return;
            }
            const auto depthTarget = graph.GetPrimaryDepthTarget();

            graph.AddGraphicsPass(
                "ViewportLayer.Clear", {}, {},
                {RenderGraphAttachment{
                    .Handle = colorTarget, .LoadOp = vk::AttachmentLoadOp::eClear,
                    .Clear = vk::ClearValue(vk::ClearColorValue(0.02f, 0.02f, 0.03f, 1.0f))
                }},
                depthTarget == kInvalidRenderGraphHandle
                    ? std::nullopt
                    : std::optional(RenderGraphAttachment{
                        .Handle = depthTarget, .LoadOp = vk::AttachmentLoadOp::eClear,
                        .Clear = vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0))
                    }),
                [](vk::CommandBuffer, RenderGraph&)
                {
                });
        }

        void OnUiRender() override
        {
            ImGui::Begin("Viewport");
            if (const auto target = m_Renderer->GetRenderTargetInfo(m_LayerTarget.Id))
            {
                ImGui::Image(
                    reinterpret_cast<ImTextureID>(target->ImGuiTexture),
                    ImVec2(static_cast<float>(target->Extent.width),
                           static_cast<float>(target->Extent.height)));
            }
            else
            {
                ImGui::TextUnformatted("Viewport buffer not ready yet.");
            }
            ImGui::End();
        }

    private:
        std::shared_ptr<Renderer> m_Renderer;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(ViewportLayer)
