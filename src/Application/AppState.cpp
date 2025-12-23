#include "AppState.h"
#include <spdlog/spdlog.h>
#include <fstream>

ParameterRegistry& AppState::Params() noexcept {
    return ParameterRegistry::Instance();
}

void AppState::LoadState(const std::filesystem::path& configPath) {
    m_configPath = configPath;

    std::filesystem::path backupPath = configPath;
    backupPath += ".backup";

    if (std::filesystem::exists(configPath)) {
        try {
            Params().LoadValuesFromYaml(configPath);
            spdlog::info("Loaded application state from {}", configPath.string());
            return;
        } catch (const std::exception& e) {
            spdlog::warn("Failed to load config file {}: {}", configPath.string(), e.what());
        }
    }

    if (std::filesystem::exists(backupPath)) {
        try {
            Params().LoadValuesFromYaml(backupPath);
            std::filesystem::copy_file(backupPath, configPath, std::filesystem::copy_options::overwrite_existing);
            spdlog::info("Loaded application state from backup {}", backupPath.string());
            return;
        } catch (const std::exception& e) {
            spdlog::warn("Failed to load backup config file {}: {}", backupPath.string(), e.what());
        }
    }

    spdlog::info("No valid config found, using defaults");
}

void AppState::SaveState(const std::filesystem::path& configPath) {
    if (!configPath.empty()) {
        m_configPath = configPath;
    }

    std::filesystem::path backupPath = m_configPath;
    backupPath += ".backup";

    try {
        if (std::filesystem::exists(m_configPath)) {
            if (std::filesystem::exists(backupPath)) {
                std::filesystem::remove(backupPath);
            }
            std::filesystem::copy_file(m_configPath, backupPath);
        }

        Params().SaveValuesToYaml(m_configPath);

    } catch (const std::exception& e) {
        spdlog::error("Failed to save config file {}: {}", m_configPath.string(), e.what());

        if (std::filesystem::exists(backupPath)) {
            try {
                if (std::filesystem::exists(m_configPath)) {
                    std::filesystem::remove(m_configPath);
                }
                std::filesystem::copy_file(backupPath, m_configPath);
                spdlog::info("Restored config from backup after save failure");
            } catch (const std::exception& restoreError) {
                spdlog::error("Failed to restore backup: {}", restoreError.what());
            }
        }
    }
}
