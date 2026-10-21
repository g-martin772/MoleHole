export module MoleHole:Application.TextureImport;

import std;
import GPP;

// Brings an image into the project so a scene can reference it by a project-relative path. Files outside the
// project are copied to assets/textures/imported/ (out of git like every texture, docs/notes/texture_sources.md).
export namespace MoleHole
{
    inline constexpr std::string_view kImportedTextureDirectory = "assets/textures/imported";

    struct TextureImportResult
    {
        std::string AssetPath; // project-relative, generic separators; empty on failure
        std::vector<std::string> Warnings;
        std::string Error;
        bool Copied = false;
    };

    // Extensions stb_image decodes (gpp DecodeTexture)
    [[nodiscard]] inline bool IsImportableTexture(const std::filesystem::path& path)
    {
        auto extension = path.extension().string();
        std::ranges::transform(extension, extension.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return std::ranges::contains(std::array<std::string_view, 9>{".png", ".jpg", ".jpeg", ".tga", ".bmp", ".psd",
                                                                     ".hdr", ".pic", ".gif"},
                                     extension);
    }

    namespace Detail
    {
        inline bool SameContents(const std::filesystem::path& a, const std::filesystem::path& b)
        {
            std::error_code ec;
            if (std::filesystem::file_size(a, ec) != std::filesystem::file_size(b, ec) || ec) return false;
            std::ifstream left(a, std::ios::binary);
            std::ifstream right(b, std::ios::binary);
            return std::equal(std::istreambuf_iterator<char>(left), std::istreambuf_iterator<char>(),
                              std::istreambuf_iterator<char>(right), std::istreambuf_iterator<char>());
        }

        inline std::optional<std::filesystem::path> RelativeInside(const std::filesystem::path& file,
                                                                   const std::filesystem::path& root)
        {
            std::error_code ec;
            const auto relative = std::filesystem::relative(std::filesystem::weakly_canonical(file, ec),
                                                            std::filesystem::weakly_canonical(root, ec), ec);
            if (ec || relative.empty() || *relative.begin() == "..") return std::nullopt;
            return relative;
        }
    }

    [[nodiscard]] inline TextureImportResult ImportTexture(const std::filesystem::path& source,
                                                           const std::filesystem::path& projectRoot)
    {
        TextureImportResult result;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(source, ec))
        {
            result.Error = "File not found: " + source.string();
            return result;
        }
        if (!IsImportableTexture(source))
        {
            result.Error = "Unsupported image format '" + source.extension().string()
                           + "': use PNG, JPEG, TGA, BMP, PSD, GIF or Radiance HDR (convert TIFF and EXR first)";
            return result;
        }
        int width = 0;
        int height = 0;
        int channels = 0;
        if (stbi_info(source.string().c_str(), &width, &height, &channels) == 0)
        {
            const char* reason = stbi_failure_reason();
            result.Error = std::string("Cannot read image: ") + (reason ? reason : "unknown error");
            return result;
        }
        if (width != 2 * height)
        {
            result.Warnings.push_back(std::format("{}x{} is not 2:1; spheres expect an equirectangular map", width, height));
        }
        if (std::max(width, height) > 16384)
        {
            result.Warnings.push_back(std::format("{}x{} exceeds 16384 px; the top mips are dropped on the GPU", width, height));
        }

        if (const auto inside = Detail::RelativeInside(source, projectRoot))
        {
            result.AssetPath = inside->generic_string();
            return result;
        }

        const auto directory = projectRoot / kImportedTextureDirectory;
        std::filesystem::create_directories(directory, ec);
        if (ec)
        {
            result.Error = "Cannot create " + directory.string() + ": " + ec.message();
            return result;
        }
        const auto stem = source.stem().string();
        const auto extension = source.extension().string();
        for (int index = 0;; ++index)
        {
            const auto name = index == 0 ? stem + extension : std::format("{}_{}{}", stem, index, extension);
            const auto target = directory / name;
            if (std::filesystem::exists(target, ec))
            {
                if (!Detail::SameContents(source, target)) continue; // a different image of the same name
            }
            else if (!std::filesystem::copy_file(source, target, ec) || ec)
            {
                result.Error = "Cannot copy to " + target.string() + ": " + ec.message();
                return result;
            }
            else
            {
                result.Copied = true;
            }
            result.AssetPath = (std::filesystem::path(kImportedTextureDirectory) / name).generic_string();
            return result;
        }
    }
}
