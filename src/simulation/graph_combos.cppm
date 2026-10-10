module;
#include <yaml-cpp/yaml.h>
export module MoleHole:Simulation.GraphCombos;

import std;
import glm;
import GPP;
import :Simulation.AnimationGraph;
import :Simulation.GraphEdit;
import :Simulation.GraphLayout;
import :Simulation.NodeRegistry;

export namespace MoleHole
{
    constexpr const char* kComboAssetKind = "Combos";
    constexpr int kComboVersion = 1;

    struct ComboOption
    {
        std::string Label;
        std::map<std::string, std::string> Vars;
    };

    // Maps a form field onto an inner node: Set "Constant" writes its constant, "Target" its entity.
    // With Values, the field (a choice index) selects one entry instead of being copied.
    struct ComboFieldTarget
    {
        std::string Node;
        std::string Set{"Constant"};
        std::vector<YAML::Node> Values;
    };

    struct ComboField
    {
        std::string Label;
        PinType Type{PinType::Float};
        bool Choice{false};
        std::vector<ComboOption> Options;
        Value Default;
        std::vector<ComboFieldTarget> Targets;
    };

    struct ComboNodeSpec
    {
        std::string Key;
        std::string Kind;
        YAML::Node Raw;
        YAML::Node Constant;
        std::string Variable;
        glm::vec2 Position{0.0f};
    };

    struct ComboTemplate
    {
        std::string Name;
        std::string Description;
        std::vector<std::string> Keywords;
        std::vector<Variable> Variables;
        std::vector<ComboField> Fields;
        std::vector<ComboNodeSpec> Nodes;
        std::vector<std::pair<std::string, std::string>> Links;
        std::string Entry;
        bool Wrap{true};
    };

    struct ComboResult
    {
        bool Ok{false};
        std::string Error;
        std::vector<int> Ids;
    };

    [[nodiscard]] inline Value ComboValueFromYaml(const YAML::Node& node, const PinType type)
    {
        const auto vec = [&node](const int n)
        {
            std::array<float, 4> v{};
            for (int i = 0; i < n; ++i) v[static_cast<std::size_t>(i)] = node[static_cast<std::size_t>(i)].as<float>();
            return v;
        };
        switch (type)
        {
        case PinType::Bool: return node.as<bool>();
        case PinType::Int: return node.as<int>();
        case PinType::Float: return node.as<float>();
        case PinType::String: return node.as<std::string>();
        case PinType::Object: return node.as<std::uint64_t>();
        case PinType::Vec2: { const auto v = vec(2); return glm::vec2(v[0], v[1]); }
        case PinType::Vec3: { const auto v = vec(3); return glm::vec3(v[0], v[1], v[2]); }
        case PinType::Vec4: { const auto v = vec(4); return glm::vec4(v[0], v[1], v[2], v[3]); }
        case PinType::Flow: break;
        }
        return std::monostate{};
    }

    [[nodiscard]] inline YAML::Node ComboValueToYaml(const Value& value)
    {
        YAML::Node node;
        const auto seq = [&node](std::initializer_list<float> items)
        {
            for (const float f : items) node.push_back(f);
            node.SetStyle(YAML::EmitterStyle::Flow);
        };
        if (const auto* b = std::get_if<bool>(&value)) node = *b;
        else if (const auto* i = std::get_if<int>(&value)) node = *i;
        else if (const auto* f = std::get_if<float>(&value)) node = *f;
        else if (const auto* str = std::get_if<std::string>(&value)) node = *str;
        else if (const auto* o = std::get_if<std::uint64_t>(&value)) node = *o;
        else if (const auto* v2 = std::get_if<glm::vec2>(&value)) seq({v2->x, v2->y});
        else if (const auto* v3 = std::get_if<glm::vec3>(&value)) seq({v3->x, v3->y, v3->z});
        else if (const auto* v4 = std::get_if<glm::vec4>(&value)) seq({v4->x, v4->y, v4->z, v4->w});
        return node;
    }

    [[nodiscard]] inline Value ConvertValue(const Value& value, const PinType type)
    {
        switch (type)
        {
        case PinType::Float: if (const auto* i = std::get_if<int>(&value)) return static_cast<float>(*i); break;
        case PinType::Int: if (const auto* f = std::get_if<float>(&value)) return static_cast<int>(*f); break;
        default: break;
        }
        return value;
    }

    [[nodiscard]] inline std::optional<ComboTemplate> ParseCombo(const std::string& text, std::string& error)
    {
        try
        {
            const YAML::Node root = YAML::Load(text);
            if (!root.IsMap() || !root["Name"] || !root["Nodes"] || !root["Nodes"].IsSequence())
            {
                error = "A combo needs a Name and a Nodes list";
                return std::nullopt;
            }
            ComboTemplate combo;
            combo.Name = root["Name"].as<std::string>();
            combo.Description = root["Description"] ? root["Description"].as<std::string>() : std::string{};
            combo.Entry = root["Entry"] ? root["Entry"].as<std::string>() : std::string{};
            combo.Wrap = root["Wrap"] ? root["Wrap"].as<bool>() : true;
            if (const auto k = root["Keywords"]; k && k.IsSequence())
            {
                for (const auto& word : k) combo.Keywords.push_back(word.as<std::string>());
            }
            if (const auto vars = root["Variables"]; vars && vars.IsSequence())
            {
                for (const auto& v : vars)
                {
                    Variable variable;
                    variable.Name = v["Name"].as<std::string>();
                    variable.Type = PinTypeFromText(v["Type"] ? v["Type"].as<std::string>() : "Float");
                    variable.Default = v["Default"] ? ComboValueFromYaml(v["Default"], variable.Type) : DefaultValueFor(variable.Type);
                    combo.Variables.push_back(std::move(variable));
                }
            }
            for (const auto& n : root["Nodes"])
            {
                ComboNodeSpec spec;
                spec.Key = n["Key"].as<std::string>();
                if (spec.Key.empty() || spec.Key.find('.') != std::string::npos ||
                    std::ranges::contains(combo.Nodes, spec.Key, &ComboNodeSpec::Key))
                {
                    error = "Bad or duplicate node key '" + spec.Key + "'";
                    return std::nullopt;
                }
                if (n["Raw"]) spec.Raw = n["Raw"];
                else if (n["Kind"]) spec.Kind = n["Kind"].as<std::string>();
                else
                {
                    error = "Node '" + spec.Key + "' needs a Kind or Raw definition";
                    return std::nullopt;
                }
                if (n["Constant"]) spec.Constant = n["Constant"];
                if (n["Variable"]) spec.Variable = n["Variable"].as<std::string>();
                if (const auto p = n["Pos"]; p && p.IsSequence() && p.size() >= 2)
                {
                    spec.Position = glm::vec2(p[0].as<float>(), p[1].as<float>());
                }
                combo.Nodes.push_back(std::move(spec));
            }
            if (const auto links = root["Links"]; links && links.IsSequence())
            {
                for (const auto& l : links) combo.Links.emplace_back(l[0].as<std::string>(), l[1].as<std::string>());
            }
            if (const auto fields = root["Fields"]; fields && fields.IsSequence())
            {
                for (const auto& f : fields)
                {
                    ComboField field;
                    field.Label = f["Label"].as<std::string>();
                    const std::string type = f["Type"] ? f["Type"].as<std::string>() : "Float";
                    field.Choice = type == "Choice";
                    field.Type = field.Choice ? PinType::Int : PinTypeFromText(type);
                    if (field.Choice)
                    {
                        for (const auto& o : f["Options"])
                        {
                            ComboOption option;
                            if (o.IsScalar()) option.Label = o.as<std::string>();
                            else
                            {
                                option.Label = o["Label"].as<std::string>();
                                if (const auto vars = o["Vars"]; vars && vars.IsMap())
                                {
                                    for (const auto& kv : vars) option.Vars[kv.first.as<std::string>()] = kv.second.as<std::string>();
                                }
                            }
                            field.Options.push_back(std::move(option));
                        }
                        if (field.Options.empty())
                        {
                            error = "Choice field '" + field.Label + "' has no options";
                            return std::nullopt;
                        }
                    }
                    field.Default = f["Default"] ? ComboValueFromYaml(f["Default"], field.Type) : DefaultValueFor(field.Type);
                    if (const auto targets = f["Targets"]; targets && targets.IsSequence())
                    {
                        for (const auto& t : targets)
                        {
                            ComboFieldTarget target;
                            target.Node = t["Node"].as<std::string>();
                            if (t["Set"]) target.Set = t["Set"].as<std::string>();
                            if (const auto values = t["Values"]; values && values.IsSequence())
                            {
                                for (const auto& v : values) target.Values.push_back(v);
                            }
                            field.Targets.push_back(std::move(target));
                        }
                    }
                    combo.Fields.push_back(std::move(field));
                }
            }
            return combo;
        }
        catch (const YAML::Exception& e)
        {
            error = e.what();
            return std::nullopt;
        }
    }

    namespace ComboDetail
    {
        inline std::string Substitute(std::string text, const std::map<std::string, std::string>& vars)
        {
            for (const auto& [key, value] : vars)
            {
                const std::string token = "${" + key + "}";
                for (auto pos = text.find(token); pos != std::string::npos; pos = text.find(token, pos + value.size()))
                {
                    text.replace(pos, token.size(), value);
                }
            }
            return text;
        }

        inline const Pin* ResolvePin(const Node& node, const std::string& name, const bool output)
        {
            const auto& pins = output ? node.Outputs : node.Inputs;
            if (!name.empty() && name[0] == '#')
            {
                const std::size_t index = static_cast<std::size_t>(std::atoi(name.c_str() + 1));
                return index < pins.size() ? &pins[index] : nullptr;
            }
            const auto it = std::ranges::find(pins, name, &Pin::Name);
            return it != pins.end() ? &*it : nullptr;
        }
    }

    // Builds the combo's nodes into `graph` as one group; the first id is the node wired to a dragged pin.
    [[nodiscard]] inline ComboResult InstantiateCombo(const ComboTemplate& combo, AnimationGraphData& graph,
                                                      const NodeRegistry& registry, const std::vector<Value>& values = {})
    {
        ComboResult result;
        AnimationGraphData work = graph;

        std::map<std::string, std::string> vars;
        std::vector<Value> fieldValues;
        for (std::size_t i = 0; i < combo.Fields.size(); ++i)
        {
            const auto& field = combo.Fields[i];
            Value value = i < values.size() ? ConvertValue(values[i], field.Type) : field.Default;
            if (field.Choice)
            {
                const int index = std::clamp(GetValueAs<int>(value, 0), 0, static_cast<int>(field.Options.size()) - 1);
                value = index;
                for (const auto& [k, v] : field.Options[static_cast<std::size_t>(index)].Vars) vars[k] = v;
            }
            fieldValues.push_back(std::move(value));
        }

        std::map<std::string, std::string> variableNames;
        for (const auto& variable : combo.Variables)
        {
            Variable fresh = variable;
            fresh.Name = UniqueVariableName(work, variable.Name);
            variableNames[variable.Name] = fresh.Name;
            work.Variables.push_back(std::move(fresh));
        }

        std::map<std::string, int> idOf;
        std::vector<int> nodeIds;
        for (const auto& spec : combo.Nodes)
        {
            Node node;
            if (!spec.Kind.empty())
            {
                const std::string kind = ComboDetail::Substitute(spec.Kind, vars);
                const NodeEntry* entry = registry.Find(kind);
                if (!entry)
                {
                    result.Error = "Unknown node kind '" + kind + "'";
                    return result;
                }
                const auto ids = entry->Spawn(work);
                if (ids.empty() || !work.FindNode(ids.front()))
                {
                    result.Error = "Kind '" + kind + "' did not create a node";
                    return result;
                }
                node = *work.FindNode(ids.front());
                work.RemoveNode(node.Id);
            }
            else
            {
                YAML::Node wrapper;
                wrapper["Nodes"].push_back(spec.Raw);
                auto parsed = GraphFromNode(wrapper);
                if (parsed.Nodes.empty())
                {
                    result.Error = "Node '" + spec.Key + "' has an invalid Raw definition";
                    return result;
                }
                node = std::move(parsed.Nodes.front());
            }

            const int oldId = node.Id;
            node.Id = work.AllocateId();
            for (auto* pins : {&node.Inputs, &node.Outputs})
            {
                for (auto& pin : *pins)
                {
                    pin.Id = node.Id * kPinIdStride + (pin.Id - oldId * kPinIdStride);
                }
            }
            node.Position = spec.Position;

            if (node.Type == NodeType::Variable)
            {
                const std::string name = spec.Variable.empty() ? node.VariableName : spec.Variable;
                const auto renamed = variableNames.find(name);
                const std::string actual = renamed != variableNames.end() ? renamed->second : name;
                node.VariableName = actual;
                const bool isSet = node.SubType == NodeSubType::VariableSet;
                node.Name = (isSet ? "Set " : "Get ") + actual;
                if (isSet && node.Inputs.size() > 1) node.Inputs[1].Name = actual;
                if (!isSet && !node.Outputs.empty()) node.Outputs[0].Name = actual;
            }
            if (spec.Constant && node.Type == NodeType::Constant && !node.Outputs.empty())
            {
                node.ConstantValue = ComboValueFromYaml(spec.Constant, node.Outputs[0].Type);
            }

            idOf[spec.Key] = node.Id;
            nodeIds.push_back(node.Id);
            work.Nodes.push_back(std::move(node));
        }

        for (std::size_t i = 0; i < combo.Fields.size(); ++i)
        {
            const auto& field = combo.Fields[i];
            for (const auto& target : field.Targets)
            {
                const auto it = idOf.find(target.Node);
                if (it == idOf.end())
                {
                    result.Error = "Field '" + field.Label + "' targets unknown node '" + target.Node + "'";
                    return result;
                }
                Node& node = *work.FindNode(it->second);
                if (target.Set == "Target")
                {
                    node.TargetGuid = GetValueAs<std::uint64_t>(fieldValues[i], 0);
                }
                else if (!node.Outputs.empty())
                {
                    const PinType type = node.Outputs[0].Type;
                    if (!target.Values.empty())
                    {
                        const auto index = static_cast<std::size_t>(std::max(0, GetValueAs<int>(fieldValues[i], 0)));
                        if (index < target.Values.size()) node.ConstantValue = ComboValueFromYaml(target.Values[index], type);
                    }
                    else
                    {
                        node.ConstantValue = ConvertValue(fieldValues[i], type);
                    }
                }
            }
        }

        const auto resolve = [&](const std::string& ref, const bool output) -> const Pin*
        {
            const std::string text = ComboDetail::Substitute(ref, vars);
            const auto dot = text.find('.');
            if (dot == std::string::npos) return nullptr;
            const auto it = idOf.find(text.substr(0, dot));
            if (it == idOf.end()) return nullptr;
            return ComboDetail::ResolvePin(*work.FindNode(it->second), text.substr(dot + 1), output);
        };
        for (const auto& [from, to] : combo.Links)
        {
            const Pin* out = resolve(from, true);
            const Pin* in = resolve(to, false);
            if (!out || !in || !TryLink(work, out->Id, in->Id))
            {
                result.Error = "Cannot link " + from + " -> " + to;
                return result;
            }
        }

        if (nodeIds.empty())
        {
            result.Error = "Combo has no nodes";
            return result;
        }
        if (!combo.Entry.empty())
        {
            const auto it = idOf.find(combo.Entry);
            if (it == idOf.end())
            {
                result.Error = "Unknown entry node '" + combo.Entry + "'";
                return result;
            }
            std::erase(nodeIds, it->second);
            nodeIds.insert(nodeIds.begin(), it->second);
        }

        result.Ids = nodeIds;
        if (combo.Wrap)
        {
            std::vector<NodeRect> rects;
            for (const int id : nodeIds)
            {
                const Node* node = work.FindNode(id);
                rects.push_back({id, node->Position, EstimateNodeSize(*node)});
            }
            const auto [position, size] = BoundsOf(rects, 24.0f, 26.0f);
            Comment comment;
            comment.Id = work.AllocateId();
            comment.Title = combo.Name;
            comment.Position = position;
            comment.Size = size;
            work.Comments.push_back(comment);
            result.Ids.push_back(comment.Id);
        }

        graph = std::move(work);
        result.Ok = true;
        return result;
    }

    [[nodiscard]] inline NodeEntry ComboToEntry(std::shared_ptr<const ComboTemplate> combo, std::shared_ptr<const NodeRegistry> registry)
    {
        NodeEntry entry;
        entry.Name = combo->Name;
        entry.Category = kCombosCategory;
        entry.Description = combo->Description;
        entry.Keywords = combo->Keywords;
        entry.Keywords.push_back("combo");
        for (const auto& field : combo->Fields)
        {
            EntryField f{field.Label, field.Type, field.Default, {}, field.Choice};
            for (const auto& option : field.Options) f.Options.push_back(option.Label);
            entry.Fields.push_back(std::move(f));
        }
        entry.SpawnWith = [combo, registry](AnimationGraphData& graph, const std::vector<Value>& values)
        {
            return InstantiateCombo(*combo, graph, *registry, values).Ids;
        };
        entry.Spawn = [spawn = entry.SpawnWith](AnimationGraphData& graph) { return spawn(graph, {}); };
        return entry;
    }

    struct ComboSource
    {
        std::string Name;
        std::string Text;
    };

    [[nodiscard]] inline std::vector<ComboSource> ReadComboSources(const GPP::AssetDirectories& assets)
    {
        std::vector<ComboSource> sources;
        for (const auto& name : assets.List(kComboAssetKind))
        {
            if (auto text = assets.Read(kComboAssetKind, name)) sources.push_back({name, std::move(*text)});
        }
        return sources;
    }

    // Parses combo templates and exposes them as palette entries; broken files are reported, never fatal.
    class ComboLibrary
    {
    public:
        ComboLibrary() : m_Registry(std::make_shared<const NodeRegistry>(BuildNodeRegistry())) {}

        void Load(const std::vector<ComboSource>& sources)
        {
            m_Entries.clear();
            m_Errors.clear();
            for (const auto& source : sources)
            {
                std::string error;
                auto combo = ParseCombo(source.Text, error);
                if (combo)
                {
                    AnimationGraphData probe;
                    if (const auto trial = InstantiateCombo(*combo, probe, *m_Registry); !trial.Ok) error = trial.Error;
                }
                if (!combo || !error.empty())
                {
                    m_Errors.push_back(source.Name + ": " + error);
                    continue;
                }
                m_Entries.push_back(ComboToEntry(std::make_shared<const ComboTemplate>(std::move(*combo)), m_Registry));
            }
        }

        [[nodiscard]] const std::vector<NodeEntry>& Entries() const { return m_Entries; }
        [[nodiscard]] const std::vector<std::string>& Errors() const { return m_Errors; }

    private:
        std::shared_ptr<const NodeRegistry> m_Registry;
        std::vector<NodeEntry> m_Entries;
        std::vector<std::string> m_Errors;
    };

    // Turns a node cluster into a template: loose constants and entity getters become exposed fields.
    [[nodiscard]] inline std::string SelectionToComboYaml(const AnimationGraphData& graph, const std::vector<int>& selectedIds,
                                                          const std::string& name, const std::string& description)
    {
        std::vector<const Node*> nodes;
        for (const auto& node : graph.Nodes)
        {
            if (std::ranges::contains(selectedIds, node.Id)) nodes.push_back(&node);
        }
        glm::vec2 origin(std::numeric_limits<float>::max());
        for (const auto* node : nodes) origin = glm::min(origin, node->Position);

        const auto keyOf = [](const Node& node) { return "n" + std::to_string(node.Id); };
        const auto pinRef = [&](const Node& node, const Pin& pin, const bool output)
        {
            const auto& pins = output ? node.Outputs : node.Inputs;
            const auto sameName = std::ranges::count(pins, pin.Name, &Pin::Name);
            const std::string ref = (sameName == 1 && !pin.Name.empty()) ? pin.Name
                                                                        : "#" + std::to_string(&pin - pins.data());
            return keyOf(node) + "." + ref;
        };

        YAML::Node root;
        root["Format"] = "MoleHoleCombo";
        root["Version"] = kComboVersion;
        root["Name"] = name;
        root["Description"] = description;
        root["Keywords"].push_back("custom");

        YAML::Node fields(YAML::NodeType::Sequence);
        std::vector<std::string> usedLabels;
        const auto uniqueLabel = [&usedLabels](std::string label)
        {
            std::string candidate = label;
            for (int i = 2; std::ranges::contains(usedLabels, candidate); ++i) candidate = label + " " + std::to_string(i);
            usedLabels.push_back(candidate);
            return candidate;
        };

        YAML::Node nodeList(YAML::NodeType::Sequence);
        for (const auto* node : nodes)
        {
            YAML::Node n;
            n["Key"] = keyOf(*node);
            AnimationGraphData single;
            single.Nodes.push_back(*node);
            n["Raw"] = GraphToNode(single)["Nodes"][0];
            n["Pos"].push_back(node->Position.x - origin.x);
            n["Pos"].push_back(node->Position.y - origin.y);
            n["Pos"].SetStyle(YAML::EmitterStyle::Flow);
            nodeList.push_back(n);

            const bool looseConstant =
                node->Type == NodeType::Constant && !node->Outputs.empty() &&
                std::ranges::none_of(graph.Links, [&](const Link& l) { return l.EndPinId == node->Outputs[0].Id; });
            if (looseConstant)
            {
                std::string label = "Value";
                for (const auto& link : graph.Links)
                {
                    if (link.StartPinId != node->Outputs[0].Id) continue;
                    if (const Pin* dest = FindPin(graph, link.EndPinId); dest && !dest->Name.empty()) label = dest->Name;
                    break;
                }
                YAML::Node field;
                field["Label"] = uniqueLabel(label);
                field["Type"] = PinTypeToText(node->Outputs[0].Type);
                field["Default"] = ComboValueToYaml(node->ConstantValue);
                YAML::Node target;
                target["Node"] = keyOf(*node);
                field["Targets"].push_back(target);
                fields.push_back(field);
            }
            else if (node->Type == NodeType::Getter)
            {
                YAML::Node field;
                field["Label"] = uniqueLabel(node->Component.empty() ? "Entity" : node->Component + " Entity");
                field["Type"] = "Object";
                field["Default"] = 0;
                YAML::Node target;
                target["Node"] = keyOf(*node);
                target["Set"] = "Target";
                field["Targets"].push_back(target);
                fields.push_back(field);
            }
        }
        root["Nodes"] = nodeList;
        if (fields.size() > 0) root["Fields"] = fields;

        YAML::Node variables(YAML::NodeType::Sequence);
        for (const auto* node : nodes)
        {
            if (node->VariableName.empty()) continue;
            const auto it = std::ranges::find(graph.Variables, node->VariableName, &Variable::Name);
            if (it == graph.Variables.end()) continue;
            bool seen = false;
            for (const auto& v : variables) seen = seen || v["Name"].as<std::string>() == it->Name;
            if (seen) continue;
            YAML::Node v;
            v["Name"] = it->Name;
            v["Type"] = PinTypeToText(it->Type);
            v["Default"] = ComboValueToYaml(it->Default);
            variables.push_back(v);
        }
        if (variables.size() > 0) root["Variables"] = variables;

        YAML::Node links(YAML::NodeType::Sequence);
        for (const auto& link : graph.Links)
        {
            const Node* from = graph.FindNodeByOutputPin(link.StartPinId);
            const Node* to = graph.FindNodeByInputPin(link.EndPinId);
            if (!from || !to || !std::ranges::contains(nodes, from) || !std::ranges::contains(nodes, to)) continue;
            const Pin* out = FindPin(graph, link.StartPinId);
            const Pin* in = FindPin(graph, link.EndPinId);
            YAML::Node l;
            l.push_back(pinRef(*from, *out, true));
            l.push_back(pinRef(*to, *in, false));
            l.SetStyle(YAML::EmitterStyle::Flow);
            links.push_back(l);
        }
        root["Links"] = links;

        std::stringstream stream;
        stream << root;
        return stream.str();
    }

    [[nodiscard]] inline std::string ComboFileStem(const std::string& name)
    {
        std::string stem;
        for (const char c : name)
        {
            stem += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
        }
        return stem.empty() ? "combo" : stem;
    }

    // Writes the template into `directory` without overwriting an existing file; returns the written path.
    inline std::optional<std::filesystem::path> SaveComboFile(const std::filesystem::path& directory, const std::string& name,
                                                              const std::string& yaml)
    {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec) return std::nullopt;
        const std::string stem = ComboFileStem(name);
        std::filesystem::path path = directory / (stem + ".yaml");
        for (int i = 2; std::filesystem::exists(path, ec); ++i) path = directory / (stem + "_" + std::to_string(i) + ".yaml");
        std::ofstream out(path, std::ios::binary);
        if (!out) return std::nullopt;
        out << yaml;
        return out.good() ? std::optional(path) : std::nullopt;
    }
}
