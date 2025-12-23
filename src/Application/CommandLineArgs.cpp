#include "CommandLineArgs.h"
#include <spdlog/spdlog.h>

void CommandLineArgs::Parse(int argc, char* argv[]) {
    m_values.clear();
    m_flags.clear();
    m_positionalArgs.clear();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg.starts_with("--")) {
            std::string flagOrKey = arg.substr(2);

            size_t equalPos = flagOrKey.find('=');
            if (equalPos != std::string::npos) {
                std::string key = flagOrKey.substr(0, equalPos);
                std::string value = flagOrKey.substr(equalPos + 1);
                m_values[NormalizeFlag(key)] = value;
                spdlog::debug("CLI: {}={}", key, value);
            } else {
                if (i + 1 < argc && !std::string(argv[i + 1]).starts_with("-")) {
                    std::string value = argv[++i];
                    m_values[NormalizeFlag(flagOrKey)] = value;
                    spdlog::debug("CLI: {}={}", flagOrKey, value);
                } else {
                    m_flags.push_back(NormalizeFlag(flagOrKey));
                    spdlog::debug("CLI: --{}", flagOrKey);
                }
            }
        } else if (arg.starts_with("-")) {
            std::string flags = arg.substr(1);
            for (char c : flags) {
                m_flags.push_back(std::string(1, c));
                spdlog::debug("CLI: -{}", c);
            }
        } else {
            m_positionalArgs.push_back(arg);
            spdlog::debug("CLI positional: {}", arg);
        }
    }
}

bool CommandLineArgs::HasFlag(const std::string& flag) const {
    std::string normalized = NormalizeFlag(flag);
    for (const auto& f : m_flags) {
        if (f == normalized) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> CommandLineArgs::GetValue(const std::string& key) const {
    std::string normalized = NormalizeFlag(key);
    auto it = m_values.find(normalized);
    if (it != m_values.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string CommandLineArgs::GetValue(const std::string& key, const std::string& defaultValue) const {
    auto value = GetValue(key);
    return value.value_or(defaultValue);
}

std::string CommandLineArgs::NormalizeFlag(const std::string& flag) {
    std::string result = flag;

    for (char& c : result) {
        if (c == '_') {
            c = '-';
        }
    }

    return result;
}

