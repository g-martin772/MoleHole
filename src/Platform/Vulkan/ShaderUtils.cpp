#include "ShaderUtils.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace ShaderUtils {

    std::string ReadFile(const char* filepath) {
        std::ifstream in(filepath, std::ios::in | std::ios::binary);
        if (in) {
            std::string contents;
            in.seekg(0, std::ios::end);
            contents.resize(in.tellg());
            in.seekg(0, std::ios::beg);
            in.read(&contents[0], contents.size());
            in.close();
            return contents;
        }
        spdlog::error("Failed to read file: {}", filepath);
        return "";
    }

    std::string ComputeHash(const std::string& data) {
        return ""; // Stub
    }

    bool IsCacheValid(const std::string& cacheKey, const char* sourcePath, const char* includePath, const char* cacheSubDir, const char* cacheExt) {
        return false; // Stub: Always recompile
    }

    bool LoadBinaryCache(const std::string& cacheKey, std::vector<uint32_t>& outSpirv, const char* cacheSubDir, const char* cacheExt) {
        return false; // Stub
    }

    void SaveBinaryCache(const std::string& cacheKey, const void* data, size_t size, uint64_t sourceModTime, uint64_t includeModTime, const char* cacheSubDir, const char* cacheExt) {
        // Stub
    }

    uint64_t GetFileModTime(const char* filepath) {
        try {
            return std::filesystem::last_write_time(filepath).time_since_epoch().count();
        } catch (...) {
            return 0;
        }
    }

}
