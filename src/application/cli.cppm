export module MoleHole:Application.Cli;

import std;
import :UI.State;

export namespace MoleHole
{
    struct CliExportArgs
    {
        bool Requested = false;
        bool ListSettings = false;
        ExportRequest Request;
        bool SceneCamera = false;
        float TimeoutSeconds = 120.0f;
        std::string Error;
    };

    [[nodiscard]] std::optional<CliExportArgs> ParseExportArgs(int argc, char* argv[]);
    void PrintSettings();
    [[nodiscard]] std::optional<std::string> ParseValidateSceneArg(int argc, char* argv[]);
    [[nodiscard]] int RunValidateScene(const std::string& path);
}
