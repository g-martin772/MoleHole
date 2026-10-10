module MoleHole;

import :Simulation.AnimationGraphProperties;
import :Simulation.AnimationGraph;
import std;
import glm;
import GPP;

namespace MoleHole
{
    namespace
    {
        PinType PinTypeOf(const GPP::FieldType type)
        {
            switch (type)
            {
            case GPP::FieldType::Bool: return PinType::Bool;
            case GPP::FieldType::Int:
            case GPP::FieldType::Enum: return PinType::Int;
            case GPP::FieldType::Vec2: return PinType::Vec2;
            case GPP::FieldType::Vec3: return PinType::Vec3;
            case GPP::FieldType::Vec4: return PinType::Vec4;
            case GPP::FieldType::String: return PinType::String;
            case GPP::FieldType::Entity: return PinType::Object;
            case GPP::FieldType::Float: default: return PinType::Float;
            }
        }

        PropertyCategory BuildCategory(const GPP::ComponentTypeInfo& info)
        {
            PropertyCategory category;
            category.ComponentName = info.Name;
            category.DisplayName = info.Name;
            for (const auto& field : info.Fields)
            {
                category.Properties.push_back(PropertyEntry{
                    .Label = field.Name,
                    .Type = PinTypeOf(field.Type),
                    .Get = [field](const GPP::Scene& scene, const entt::entity entity) -> Value
                    {
                        return field.Get(scene.Registry(), entity);
                    },
                    .Set = [field](GPP::Scene& scene, const entt::entity entity, const Value& value)
                    {
                        if (field.Set) { field.Set(scene.Registry(), entity, value); }
                    },
                });
            }
            return category;
        }

        struct CategoryCache
        {
            std::mutex Mutex;
            std::map<std::string, std::unique_ptr<PropertyCategory>> ByName;
        };

        CategoryCache& Cache()
        {
            static CategoryCache cache;
            return cache;
        }

        const PropertyCategory* Resolve(const GPP::ComponentTypeInfo& info)
        {
            if (!info.GraphExposed) { return nullptr; }
            auto& cache = Cache();
            std::scoped_lock lock(cache.Mutex);
            auto& slot = cache.ByName[info.Name];
            if (!slot || slot->Properties.size() != info.Fields.size())
            {
                slot = std::make_unique<PropertyCategory>(BuildCategory(info));
            }
            return slot.get();
        }
    }

    std::vector<const PropertyCategory*> GetPropertyCategories()
    {
        std::vector<const PropertyCategory*> result;
        GPP::ComponentRegistry::Instance().ForEach([&result](const GPP::ComponentTypeInfo& info)
        {
            if (const auto* category = Resolve(info)) { result.push_back(category); }
        });
        return result;
    }

    const PropertyCategory* FindPropertyCategory(const std::string& componentName)
    {
        const auto* info = GPP::ComponentRegistry::Instance().FindByName(componentName);
        return info ? Resolve(*info) : nullptr;
    }
}
