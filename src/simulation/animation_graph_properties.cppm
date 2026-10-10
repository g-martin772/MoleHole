export module MoleHole:Simulation.AnimationGraphProperties;

import std;
import glm;
import GPP;
import :Simulation.AnimationGraph;

export namespace MoleHole
{
    struct PropertyEntry
    {
        std::string Label;
        PinType Type{PinType::Float};
        std::function<Value(const GPP::Scene&, entt::entity)> Get; // returns std::monostate if the component is missing
        std::function<void(GPP::Scene&, entt::entity, const Value&)> Set; // no-op if component missing or Value holds the wrong alternative
    };

    struct PropertyCategory
    {
        std::string ComponentName;
        std::string DisplayName;
        std::vector<PropertyEntry> Properties;
    };

    [[nodiscard]] std::vector<const PropertyCategory*> GetPropertyCategories();
    [[nodiscard]] const PropertyCategory* FindPropertyCategory(const std::string& componentName);
}
