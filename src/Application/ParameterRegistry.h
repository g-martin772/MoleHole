#pragma once

#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <mutex>
#include <optional>
#include <cstdint>
#include <glm/vec3.hpp>
#include <yaml-cpp/yaml.h>

constexpr uint64_t FnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t FnvPrime = 1099511628211ull;

template<size_t N>
consteval uint64_t ConstexprFnv1a(const char (&str)[N]) {
    uint64_t h = FnvOffsetBasis;
    for (std::size_t i = 0; i < N - 1; ++i) {
        h ^= static_cast<uint8_t>(str[i]);
        h *= FnvPrime;
    }
    return h;
}

inline uint64_t RuntimeFnv1a(const std::string_view s) {
    uint64_t h = FnvOffsetBasis;
    for (unsigned char c: s) {
        h ^= c;
        h *= FnvPrime;
    }
    return h;
}

struct ParameterHandle {
    uint64_t m_Id = 0;

    constexpr ParameterHandle() noexcept = default;

    constexpr explicit ParameterHandle(const uint64_t id) noexcept : m_Id(id) {
    }

    template<size_t N>
    consteval explicit ParameterHandle(const char (&str)[N]) noexcept : m_Id(ConstexprFnv1a(str)) {
    }

    explicit ParameterHandle(const std::string_view str) noexcept : m_Id(RuntimeFnv1a(str)) {
    }

    constexpr bool IsValid() const noexcept { return m_Id != 0; }
    constexpr bool operator==(const ParameterHandle &o) const noexcept { return m_Id == o.m_Id; }
};

enum class DebugMode {
    Normal = 0,
    InfluenceZones = 1,
    DeflectionMagnitude = 2,
    GravitationalField = 3,
    SphericalShape = 4,
    DebugLUT = 5,
    GravityGrid = 6,
    ObjectPaths = 7
};

enum class ParameterGroup {
    Window,
    Camera,
    Rendering,
    Physics,
    Debug,
    Simulation,
    Application,
    Export
};

enum class ParameterType {
    Bool,
    Int,
    Float,
    String,
    Vec2,
    Vec3,
    Vec4,
    Enum,
    StringVector
};

using ParameterValue = std::variant<bool, int, float, std::string, glm::vec3, std::vector<std::string>>;

struct ParameterMetadata {
    uint64_t id;
    std::string name;
    std::string displayName;
    std::string tooltip;
    ParameterType type;
    ParameterGroup group;
    ParameterValue defaultValue;

    float minValue = 0.0f;
    float maxValue = 0.0f;
    float dragSpeed = 1.0f;

    std::vector<std::string> scaleValueNames;
    std::vector<float> scaleValues;
    std::vector<std::string> enumValues;

    bool isReadOnly = false;
    bool showInUI = true;
};

class ParameterRegistry {
public:
    static ParameterRegistry &Instance() noexcept;

    void LoadDefinitionsFromYaml(const std::filesystem::path &path);
    void LoadValuesFromYaml(const std::filesystem::path &path);
    void SaveValuesToYaml(const std::filesystem::path &path) const;

    void RegisterParameter(const ParameterMetadata &meta);

    bool Has(const ParameterHandle &handle) const noexcept;

    template<typename T>
    void Set(const ParameterHandle &handle, const T &value);

    template<typename T>
    T Get(const ParameterHandle &handle, const T &fallback) const;

    std::optional<ParameterMetadata> GetMetadata(const ParameterHandle &handle) const;

    const std::unordered_map<uint64_t, ParameterMetadata>& GetAllMetadata() const noexcept;

private:
    ParameterRegistry() = default;

    mutable std::mutex m_Mutex;
    std::unordered_map<uint64_t, ParameterValue> m_Values;
    std::unordered_map<uint64_t, ParameterMetadata> m_Meta;

    static ParameterType ParseType(const std::string &typeStr);
    static ParameterGroup ParseGroup(const std::string &groupStr);
    static ParameterValue ParseValueNode(const YAML::Node &node, ParameterType type);
    static YAML::Node ValueToYamlNode(const ParameterValue &value);
};
