import GPP;
import MoleHole;
import std;

using namespace GPP;

namespace
{
    constexpr std::uint32_t kViewportBufferId = 1;

    struct MainLayer final : public GuiLayer
    {
        using Dependencies = std::tuple<Logger>;

        explicit MainLayer(const std::shared_ptr<Logger>& logger) : GuiLayer(logger)
        {
        }

        void OnAttach() override
        {
            m_Logger->Info("MainLayer attached");
        }
    };
}

int main(int argc, char* argv[])
{
    MoleHole::RegisterComponents();

    auto builder = GuiApplicationBuilder();

    builder.Configuration
           .AddJsonFile("config.json")
           .AddCommandLine(argc, argv)
           .AddEnvironmentVariables();

    builder.ConfigureImGui({"DockingEnable", "ViewportsEnable"}, true);
    builder.SetTheme("molehole_theme.so", true);

    builder.AddGuiLayer<MainLayer>().SetWindowTarget("main");

    builder.AddHotReloadableLayer("viewport", "molehole_viewport_layer.so")
           .SetBufferTarget(kViewportBufferId);

    auto app = builder.Build();
    return app->Run();
}
