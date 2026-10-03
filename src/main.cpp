import GPP;
import MoleHole;
import std;

using namespace GPP;

namespace
{
    constexpr std::uint32_t kViewportBufferId = 1;
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

    builder.AddHotReloadableLayer("viewport", "molehole_viewport_layer.so")
           .SetBufferTarget(kViewportBufferId)
           .SetWindowTarget("main");

    auto app = builder.Build();
    return app->Run();
}
