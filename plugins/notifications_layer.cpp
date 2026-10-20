#include <imgui.h>

import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    struct NotificationsLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger>;

        explicit NotificationsLayer(const std::shared_ptr<Logger>& logger) : HotReloadableLayer(logger) {}

        void OnUiRender() override { m_Overlay.Draw(*m_Logger->Notifications()); }

    private:
        NotificationOverlay m_Overlay;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(NotificationsLayer)
