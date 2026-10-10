#pragma once

#include <filesystem>
#include "ParameterRegistry.h"

class AppState {
public:
    void LoadState(const std::filesystem::path &configPath = "config.yaml");
    void SaveState(const std::filesystem::path &configPath = "config.yaml");

    static ParameterRegistry& Params() noexcept;
private:
    std::filesystem::path m_configPath;
};
