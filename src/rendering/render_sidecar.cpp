module;
#include <yaml-cpp/yaml.h>
module MoleHole;

import :Rendering.Sidecar;
import std;
import GPP;

using namespace GPP;

namespace MoleHole
{
    std::uint64_t HashText(const std::string_view text) noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (const unsigned char c : text)
        {
            hash ^= c;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    std::string HashHex(const std::string_view text)
    {
        return std::format("{:016x}", HashText(text));
    }

    std::string ReadGitCommit(const std::filesystem::path& repoRoot)
    {
        const auto read = [](const std::filesystem::path& file) -> std::optional<std::string>
        {
            std::ifstream stream(file);
            if (!stream) return std::nullopt;
            std::string line;
            std::getline(stream, line);
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
            return line;
        };
        const auto shorten = [](const std::string& hash) { return hash.substr(0, std::min<std::size_t>(hash.size(), 12)); };

        const auto head = read(repoRoot / ".git" / "HEAD");
        if (!head || head->empty()) return "unknown";
        constexpr std::string_view kRef = "ref: ";
        if (!head->starts_with(kRef)) return shorten(*head);

        const auto ref = head->substr(kRef.size());
        if (const auto loose = read(repoRoot / ".git" / ref); loose && !loose->empty()) return shorten(*loose);

        std::ifstream packed(repoRoot / ".git" / "packed-refs");
        std::string line;
        while (std::getline(packed, line))
        {
            if (line.empty() || line.front() == '#' || line.front() == '^') continue;
            const auto space = line.find(' ');
            if (space != std::string::npos && line.substr(space + 1) == ref) return shorten(line.substr(0, space));
        }
        return "unknown";
    }

    std::filesystem::path SidecarPathFor(const std::filesystem::path& output)
    {
        auto path = output;
        path.replace_extension(".render.yaml");
        return path;
    }

    std::string BuildRenderSidecar(const RenderSidecarInfo& info)
    {
        YAML::Node root;
        root["Version"] = 1;
        root["Timestamp"] = info.Timestamp;
        root["GitCommit"] = info.GitCommit;
        root["WallSeconds"] = info.WallSeconds;

        auto output = root["Output"];
        output["Path"] = info.OutputPath;
        output["Kind"] = info.Video ? "video" : "image";
        output["Width"] = info.Width;
        output["Height"] = info.Height;
        output["Frames"] = info.Frames;
        if (info.Video) output["Framerate"] = info.Framerate;

        auto scene = root["Scene"];
        scene["Path"] = info.ScenePath;
        scene["Name"] = info.SceneName;
        scene["Hash"] = HashHex(info.SceneYaml);

        auto camera = root["Camera"];
        camera["PositionM"] = info.CameraPositionM;
        camera["Yaw"] = info.CameraYaw;
        camera["Pitch"] = info.CameraPitch;
        camera["Fov"] = info.CameraFov;

        auto profile = root["Profile"];
        profile["Name"] = info.ProfileName;
        YAML::Node overrides(YAML::NodeType::Map);
        for (const auto& [key, value] : info.Overrides) overrides[key] = value;
        if (overrides.size() > 0) profile["Overrides"] = overrides;
        // Every key, so the render does not depend on what the defaults are later
        root["Settings"] = RenderSettingRegistry().Encode(info.Settings);

        auto gpu = root["Gpu"];
        gpu["Name"] = info.Gpu.DeviceName;
        gpu["Driver"] = info.Gpu.DriverVersion;
        gpu["Type"] = std::string(GpuTypeName(info.Gpu.Type));
        gpu["Reason"] = info.Gpu.Reason;
        for (const auto key : GpuCapability::kAll) gpu["Capabilities"][std::string(key)] = info.Gpu.Has(key);

        return YAML::Dump(root);
    }

    bool WriteRenderSidecar(const std::filesystem::path& output, const RenderSidecarInfo& info)
    {
        std::ofstream file(SidecarPathFor(output), std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file << BuildRenderSidecar(info) << '\n';
        return static_cast<bool>(file);
    }
}
