#include <imgui.h>

import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    struct LogWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, UiState>;

        LogWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_UiState(std::move(uiState))
        {
        }

        void OnUiRender() override
        {
            if (!m_UiState->ShowLogWindow) return;
            ImGui::SetNextWindowSize(ImVec2(760.0f, 320.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Log", &m_UiState->ShowLogWindow)) m_Panel.Draw(*m_Logger->Buffer());
            ImGui::End();
        }

    private:
        std::shared_ptr<UiState> m_UiState;
        LogPanel m_Panel;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(LogWindowLayer)
