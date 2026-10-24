module;
#include <yaml-cpp/yaml.h>
module MoleHole;

import :Application.ExportSupport;
import std;

namespace MoleHole
{
    std::string_view ExportFormatName(const ExportFormat format)
    {
        switch (format)
        {
        case ExportFormat::Video: return "Video";
        case ExportFormat::Sequence: return "Sequence";
        default: return "Image";
        }
    }

    std::optional<ExportFormat> ExportFormatFromName(const std::string_view name)
    {
        for (const auto format : {ExportFormat::Image, ExportFormat::Video, ExportFormat::Sequence})
        {
            if (ExportFormatName(format) == name) return format;
        }
        return std::nullopt;
    }

    std::string FormatDuration(const double seconds)
    {
        const auto total = static_cast<long long>(std::max(0.0, seconds) + 0.5);
        if (total >= 3600) return std::format("{}h {:02}m {:02}s", total / 3600, total / 60 % 60, total % 60);
        if (total >= 60) return std::format("{}m {:02}s", total / 60, total % 60);
        return std::format("{}s", total);
    }

    std::string FormatExportTiming(const double elapsedSeconds, const double remainingSeconds)
    {
        if (remainingSeconds < 0.0) return std::format("Elapsed {}", FormatDuration(elapsedSeconds));
        return std::format("Elapsed {} | ETA {}", FormatDuration(elapsedSeconds), FormatDuration(remainingSeconds));
    }

    std::optional<double> EstimateRemainingSeconds(const double secondsSinceFirstFrame, const int framesDone,
                                                   const int totalFrames)
    {
        if (totalFrames <= 0 || framesDone >= totalFrames) return framesDone > 0 && totalFrames > 0 ? std::optional(0.0) : std::nullopt;
        if (framesDone < 2 || !(secondsSinceFirstFrame > 0.0)) return std::nullopt;
        return secondsSinceFirstFrame / static_cast<double>(framesDone - 1) * static_cast<double>(totalFrames - framesDone);
    }

    std::string SanitizeSequenceName(const std::string_view name)
    {
        std::string result;
        for (const char c : name)
        {
            const bool bad = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                             c == '>' || c == '|' || static_cast<unsigned char>(c) < 0x20;
            result.push_back(bad ? '_' : c);
        }
        const auto first = result.find_first_not_of(' ');
        const auto last = result.find_last_not_of(' ');
        result = first == std::string::npos ? std::string() : result.substr(first, last - first + 1);
        while (!result.empty() && result.front() == '.') result.erase(result.begin());
        return result.empty() ? "frame" : result;
    }

    std::filesystem::path SequenceFramePath(const std::filesystem::path& directory, const std::string_view name,
                                            const int frameNumber)
    {
        return directory / std::format("{}_{:05d}.png", SanitizeSequenceName(name), frameNumber);
    }

    std::filesystem::path NextSequenceDirectory(const std::filesystem::path& parent)
    {
        for (int index = 1;; ++index)
        {
            auto candidate = parent / std::format("sequence_{:03d}", index);
            if (!std::filesystem::exists(candidate)) return candidate;
        }
    }

    void ExportSettings::Clamp()
    {
        Width = std::clamp(Width, 1, 16384);
        Height = std::clamp(Height, 1, 16384);
        DurationSeconds = std::isfinite(DurationSeconds) ? std::max(0.1f, DurationSeconds) : 5.0f;
        Framerate = std::clamp(Framerate, 1, 240);
        RayStepSize = std::isfinite(RayStepSize) ? std::clamp(RayStepSize, 0.001f, 0.05f) : 0.01f;
        MaxRaySteps = std::max(1000, MaxRaySteps);
        SequenceName = SanitizeSequenceName(SequenceName);
    }

    YAML::Node ExportSettings::ToYaml() const
    {
        YAML::Node node;
        node["Format"] = std::string(ExportFormatName(Format));
        node["Width"] = Width;
        node["Height"] = Height;
        node["DurationSeconds"] = DurationSeconds;
        node["Framerate"] = Framerate;
        node["Profile"] = Profile;
        node["OverrideQuality"] = OverrideQuality;
        node["RayStepSize"] = RayStepSize;
        node["MaxRaySteps"] = MaxRaySteps;
        node["SequenceFolder"] = SequenceFolder;
        node["SequenceName"] = SequenceName;
        return node;
    }

    ExportSettings ExportSettings::FromYaml(const YAML::Node& node)
    {
        ExportSettings settings;
        if (!node || !node.IsMap()) return settings;
        auto read = [&](const char* key, auto& target)
        {
            const auto value = node[key];
            if (!value || !value.IsScalar()) return;
            try
            {
                target = value.as<std::remove_reference_t<decltype(target)>>();
            }
            catch (const YAML::Exception&)
            {
            }
        };
        std::string format;
        read("Format", format);
        if (const auto parsed = ExportFormatFromName(format)) settings.Format = *parsed;
        read("Width", settings.Width);
        read("Height", settings.Height);
        read("DurationSeconds", settings.DurationSeconds);
        read("Framerate", settings.Framerate);
        read("Profile", settings.Profile);
        read("OverrideQuality", settings.OverrideQuality);
        read("RayStepSize", settings.RayStepSize);
        read("MaxRaySteps", settings.MaxRaySteps);
        read("SequenceFolder", settings.SequenceFolder);
        read("SequenceName", settings.SequenceName);
        settings.Clamp();
        return settings;
    }
}
