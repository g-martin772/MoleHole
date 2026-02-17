#pragma once

class CommandLineArgs {
public:
    CommandLineArgs() = default;

    void Parse(int argc, char* argv[]);

    bool HasFlag(const std::string& flag) const;
    std::optional<std::string> GetValue(const std::string& key) const;
    std::string GetValue(const std::string& key, const std::string& defaultValue) const;
    int GetValueInt(const std::string& key, int defaultValue) const;
    float GetValueFloat(const std::string& key, float defaultValue) const;

    bool ShouldShowIntro() const { return !HasFlag("no-flashscreen"); }
    bool IsHeadless() const { return HasFlag("headless"); }
    bool ShouldExitOnComplete() const { return HasFlag("exit-on-complete"); }

    const std::vector<std::string>& GetPositionalArgs() const { return m_positionalArgs; }

private:
    std::unordered_map<std::string, std::string> m_values;
    std::vector<std::string> m_flags;
    std::vector<std::string> m_positionalArgs;

    static std::string NormalizeFlag(const std::string& flag);
};

