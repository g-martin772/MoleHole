module;
#include <yaml-cpp/yaml.h>
export module MoleHole:Application.ExportSupport;

import std;

export namespace MoleHole
{
    enum class ExportFormat { Image, Video, Sequence };

    [[nodiscard]] std::string_view ExportFormatName(ExportFormat format);
    [[nodiscard]] std::optional<ExportFormat> ExportFormatFromName(std::string_view name);

    [[nodiscard]] std::string FormatDuration(double seconds);
    [[nodiscard]] std::string FormatExportTiming(double elapsedSeconds, double remainingSeconds);
    [[nodiscard]] std::optional<double> EstimateRemainingSeconds(double secondsSinceFirstFrame, int framesDone, int totalFrames);

    [[nodiscard]] std::string SanitizeSequenceName(std::string_view name);
    [[nodiscard]] std::filesystem::path SequenceFramePath(const std::filesystem::path& directory, std::string_view name,
                                                          int frameNumber);
    [[nodiscard]] std::filesystem::path NextSequenceDirectory(const std::filesystem::path& parent);

    struct ExportSettings
    {
        ExportFormat Format = ExportFormat::Image;
        int Width = 1920;
        int Height = 1080;
        float DurationSeconds = 5.0f;
        int Framerate = 30;
        std::string Profile;
        bool OverrideQuality = false;
        float RayStepSize = 0.01f;
        int MaxRaySteps = 100000;
        std::string SequenceFolder;
        std::string SequenceName = "frame";

        bool operator==(const ExportSettings&) const = default;

        void Clamp();
        [[nodiscard]] YAML::Node ToYaml() const;
        [[nodiscard]] static ExportSettings FromYaml(const YAML::Node& node);
    };
}
