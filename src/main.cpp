import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

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

    builder.Services.AddSingleton<UiState>();

    builder.AddHotReloadableLayer("sidebar", "molehole_sidebar_layer.so").SetWindowTarget("main");
    builder.AddHotReloadableLayer("topbar", "molehole_topbar_layer.so").SetWindowTarget("main");
    builder.AddHotReloadableLayer("scene-window", "molehole_scene_window_layer.so").SetWindowTarget("main");
    builder.AddHotReloadableLayer("camera-window", "molehole_camera_window_layer.so").SetWindowTarget("main");
    builder.AddHotReloadableLayer("debug-window", "molehole_debug_window_layer.so").SetWindowTarget("main");
    builder.AddHotReloadableLayer("system-window", "molehole_system_window_layer.so").SetWindowTarget("main");
    builder.AddHotReloadableLayer("viewport-hud", "molehole_viewport_hud_layer.so").SetWindowTarget("main");
    builder.AddHotReloadableLayer("simulation-controls", "molehole_simulation_controls_layer.so")
           .SetWindowTarget("main");

    builder.AddHotReloadableLayer("viewport", "molehole_viewport_layer.so")
           .SetBufferTarget(kViewportBufferId);

    auto app = builder.Build();
    return app->Run();
}
