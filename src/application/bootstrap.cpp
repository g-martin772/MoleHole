module MoleHole;

import :Application.Bootstrap;
import :UI.State;
import std;
import GPP;

using namespace GPP;

namespace MoleHole
{
    AppInfo MakeAppInfo(const std::string_view version)
    {
        AppInfo info;
        info.Id = "molehole";
        info.Name = "MoleHole";
        info.Organization = "MoleHole";
        info.Version = std::string(version);
        info.Summary = "Black hole simulation and visualization";
        info.Categories = "Science;Physics;";
        info.IconFile = "assets/branding/molehole.svg";
        return info;
    }

    void EnsureRuntimeDirectories(const AppPaths& paths)
    {
        std::vector<std::filesystem::path> directories{
            paths.CacheDir() / "hot-reload-cache", paths.CacheDir() / "shader-cache", paths.ConfigDir(), paths.LogDir(),
            paths.UserSubdir("exports")};
        if (paths.IsInstalled())
        {
            directories.push_back(paths.TempDir());
            for (const char* name : {"scenes", "scripts", "combos", "models"})
            {
                directories.push_back(paths.UserSubdir(name));
            }
        }
        for (const auto& directory : directories)
        {
            std::error_code error;
            std::filesystem::create_directories(directory, error);
        }
    }

    std::unordered_map<std::string, std::string> InstalledAssetOverrides(const AppPaths& paths)
    {
        std::unordered_map<std::string, std::string> values;
        const auto set = [&](const std::string& key, const std::vector<std::filesystem::path>& directories)
        {
            for (std::size_t index = 0; index < directories.size(); ++index)
            {
                values[std::format("{}:{}", key, index)] = directories[index].string();
            }
        };
        const auto& resources = paths.ResourceDir();
        set("GPP:Assets:Scripts:Directories", {paths.UserSubdir("scripts"), resources / "scripts"});
        set("GPP:Assets:Combos:Directories", {paths.UserSubdir("combos"), resources / "combos"});
        set("GPP:Graphics:Render:ShaderAssetDirectories", {resources / "shaders", resources / "shaders" / "pbr"});
        set("GPP:Graphics:Fonts:FontDirectories", {resources / "font"});
        return values;
    }

    std::unordered_map<std::string, std::string> LauncherWindowOverrides(const std::string_view title)
    {
        return {
            {"GPP:Graphics:Window:Width", "820"},
            {"GPP:Graphics:Window:Height", "520"},
            {"GPP:Graphics:Window:Resizable", "false"},
            {"GPP:Graphics:Window:Center", "true"},
            {"GPP:Graphics:Window:Title", std::string(title)},
            {"GPP:Graphics:Window:ImGui:EnableDockSpace", "false"},
            {"GPP:Graphics:Window:ImGui:IniFile", "none"}};
    }

    void RegisterLayers(GuiApplicationBuilder& builder, const bool launcherMode, const bool exporting)
    {
        const auto addWindowLayer = [&](const std::string& id)
        {
            auto library = id;
            std::ranges::replace(library, '-', '_');
            builder.AddHotReloadableLayer(id, std::format("molehole_{}_layer.so", library)).SetWindowTarget("main");
        };

        if (launcherMode)
        {
            addWindowLayer("launcher");
            addWindowLayer("notifications");
        }
        else if (!exporting)
        {
            builder.AddHotReloadableLayer("intro", "molehole_intro_layer.so").SetBufferTarget(kIntroBufferId);
            for (const auto* id : {"dock-layout", "sidebar", "topbar", "scene-window", "camera-window",
                                   "simulation-window", "render-window", "system-window", "profiler-window", "viewport-hud",
                                   "simulation-controls", "general-relativity-window", "science-window",
                                   "animation-graph-window", "tutorial-overlay", "log-window", "undo-history-window",
                                   "notifications"})
            {
                addWindowLayer(id);
            }
        }

        if (exporting)
        {
            builder.AddHotReloadableLayer("animation-graph-window", "molehole_animation_graph_window_layer.so")
                   .SetBufferTarget(kViewportBufferId);
        }
        if (!launcherMode)
        {
            builder.AddHotReloadableLayer("viewport", "molehole_viewport_layer.so").SetBufferTarget(kViewportBufferId);
        }
    }
}
