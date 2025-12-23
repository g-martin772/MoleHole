#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

class CommandLineArgs {
public:
    CommandLineArgs() = default;

    void Parse(int argc, char* argv[]);

    bool HasFlag(const std::string& flag) const;
    std::optional<std::string> GetValue(const std::string& key) const;
    std::string GetValue(const std::string& key, const std::string& defaultValue) const;

    bool ShouldShowIntro() const { return !HasFlag("no-flashscreen"); }

    const std::vector<std::string>& GetPositionalArgs() const { return m_positionalArgs; }

private:
    std::unordered_map<std::string, std::string> m_values;
    std::vector<std::string> m_flags;
    std::vector<std::string> m_positionalArgs;

    static std::string NormalizeFlag(const std::string& flag);
};

