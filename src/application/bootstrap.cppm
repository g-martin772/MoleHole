export module MoleHole:Application.Bootstrap;

import std;
import GPP;

export namespace MoleHole
{
    [[nodiscard]] GPP::AppInfo MakeAppInfo(std::string_view version);
    void EnsureRuntimeDirectories(const GPP::AppPaths& paths);
    [[nodiscard]] std::unordered_map<std::string, std::string> InstalledAssetOverrides(const GPP::AppPaths& paths);
    [[nodiscard]] std::unordered_map<std::string, std::string> LauncherWindowOverrides(std::string_view title);
    void RegisterLayers(GPP::GuiApplicationBuilder& builder, bool launcherMode, bool exporting);
}
