#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ShaderUtils {

    std::string ReadFile(const char* filepath);
    std::string ComputeHash(const std::string& data);
    bool IsCacheValid(const std::string& cacheKey, const char* sourcePath, const char* includePath, const char* cacheSubDir, const char* cacheExt);
    bool LoadBinaryCache(const std::string& cacheKey, std::vector<uint32_t>& outSpirv, const char* cacheSubDir, const char* cacheExt);
    void SaveBinaryCache(const std::string& cacheKey, const void* data, size_t size, uint64_t sourceModTime, uint64_t includeModTime, const char* cacheSubDir, const char* cacheExt);
    uint64_t GetFileModTime(const char* filepath);

}
