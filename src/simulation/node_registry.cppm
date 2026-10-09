export module MoleHole:Simulation.NodeRegistry;

import std;
import glm;
import :Simulation.AnimationGraph;
import :Simulation.AnimationGraphProperties;
import :Simulation.GraphEdit;
import :UI.FuzzySearch;

export namespace MoleHole
{
    constexpr const char* kCombosCategory = "Combos";
    constexpr std::size_t kMaxRecentNodes = 8;

    // One user-editable value shown in the form that precedes spawning an entry (used by combos).
    struct EntryField
    {
        std::string Label;
        PinType Type{PinType::Float};
        Value Default;
        std::vector<std::string> Options;
        bool Choice{false};
    };

    struct NodeEntry
    {
        std::string Name;
        std::string Category;
        std::string Description;
        std::vector<std::string> Keywords;
        // Adds the node(s) to the graph at the origin and returns their ids; the first is the one wired to a dragged pin.
        std::function<std::vector<int>(AnimationGraphData&)> Spawn;
        std::vector<EntryField> Fields;
        std::function<std::vector<int>(AnimationGraphData&, const std::vector<Value>&)> SpawnWith;
    };

    struct PinFilter
    {
        PinType Type{PinType::Flow};
        bool DraggedFromOutput{true};
    };

    [[nodiscard]] inline std::string NodeKindKey(const Node& node)
    {
        return std::to_string(static_cast<int>(node.Type)) + ":" + std::to_string(static_cast<int>(node.SubType)) + ":" +
               node.Component;
    }

    [[nodiscard]] inline std::vector<std::string> PushRecent(std::vector<std::string> recents, const std::string& name,
                                                             const std::size_t limit = kMaxRecentNodes)
    {
        std::erase(recents, name);
        recents.insert(recents.begin(), name);
        if (recents.size() > limit) recents.resize(limit);
        return recents;
    }

    [[nodiscard]] inline int MoveSelection(const int selected, const int delta, const int count)
    {
        if (count <= 0) return 0;
        return ((selected + delta) % count + count) % count;
    }

    [[nodiscard]] inline NodeEntry SingleNodeEntry(std::string name, std::string category, std::string description,
                                                   std::vector<std::string> keywords,
                                                   std::function<Node(int)> make)
    {
        NodeEntry entry{std::move(name), std::move(category), std::move(description), std::move(keywords), {}};
        entry.Spawn = [make = std::move(make)](AnimationGraphData& graph)
        {
            graph.Nodes.push_back(make(graph.AllocateId()));
            return std::vector<int>{graph.Nodes.back().Id};
        };
        return entry;
    }

    [[nodiscard]] inline const Node* ProbeNode(const NodeEntry& entry, AnimationGraphData& scratch)
    {
        scratch = AnimationGraphData{};
        const auto ids = entry.Spawn(scratch);
        return ids.empty() ? nullptr : scratch.FindNode(ids.front());
    }

    [[nodiscard]] inline bool EntryAcceptsPin(const NodeEntry& entry, const PinFilter& filter)
    {
        AnimationGraphData scratch;
        const Node* probe = ProbeNode(entry, scratch);
        return probe && FirstCompatiblePin(*probe, filter.Type, filter.DraggedFromOutput) != nullptr;
    }

    struct PaletteResult
    {
        const NodeEntry* Entry{nullptr};
        int Score{0};
        bool Recent{false};
    };

    class NodeRegistry
    {
    public:
        void Add(NodeEntry entry)
        {
            AnimationGraphData scratch;
            if (const Node* probe = ProbeNode(entry, scratch))
            {
                m_Descriptions.try_emplace(NodeKindKey(*probe), entry.Description);
            }
            m_Entries.push_back(std::move(entry));
        }

        // Extension point for combo nodes: they show up under the "Combos" category.
        void AddCombo(NodeEntry entry)
        {
            entry.Category = kCombosCategory;
            Add(std::move(entry));
        }

        // Swaps the whole set of combo entries (used when combo templates are hot-reloaded).
        void ReplaceCombos(std::vector<NodeEntry> entries)
        {
            std::erase_if(m_Entries, [](const NodeEntry& e) { return e.Category == kCombosCategory; });
            for (auto& entry : entries) AddCombo(std::move(entry));
        }

        [[nodiscard]] const NodeEntry* Find(const std::string_view name) const
        {
            const auto it = std::ranges::find(m_Entries, name, &NodeEntry::Name);
            return it != m_Entries.end() ? &*it : nullptr;
        }

        [[nodiscard]] const std::vector<NodeEntry>& Entries() const { return m_Entries; }

        [[nodiscard]] std::string Describe(const Node& node) const
        {
            const auto it = m_Descriptions.find(NodeKindKey(node));
            return it != m_Descriptions.end() ? it->second : std::string{};
        }

        // `extra` holds graph-dependent entries (e.g. one per variable) and must outlive the returned pointers.
        [[nodiscard]] std::vector<PaletteResult> Search(const std::string_view query, const std::optional<PinFilter>& filter,
                                                        const std::vector<std::string>& recents,
                                                        const std::vector<NodeEntry>& extra = {}) const
        {
            std::vector<PaletteResult> results;
            const auto consider = [&](const NodeEntry& entry)
            {
                if (filter && !EntryAcceptsPin(entry, *filter)) return;
                const auto recentIt = std::ranges::find(recents, entry.Name);
                const bool recent = recentIt != recents.end();
                int score = 0;
                if (!query.empty())
                {
                    std::optional<int> best = FuzzyScore(query, entry.Name);
                    if (best) *best += 40;
                    if (const auto s = FuzzyScore(query, entry.Category + " " + entry.Name); s && (!best || *s > *best)) best = s;
                    for (const auto& keyword : entry.Keywords)
                    {
                        if (const auto s = FuzzyScore(query, keyword); s && (!best || *s > *best)) best = s;
                    }
                    if (!best) return;
                    score = *best;
                    if (recent) score += 5;
                }
                results.push_back({&entry, score, recent});
            };
            for (const auto& entry : m_Entries) consider(entry);
            for (const auto& entry : extra) consider(entry);

            const auto recentRank = [&](const PaletteResult& r)
            {
                return static_cast<int>(std::ranges::find(recents, r.Entry->Name) - recents.begin());
            };
            std::ranges::stable_sort(results, [&](const PaletteResult& a, const PaletteResult& b)
            {
                if (!query.empty())
                {
                    if (a.Score != b.Score) return a.Score > b.Score;
                    return a.Entry->Name < b.Entry->Name;
                }
                if (a.Recent != b.Recent) return a.Recent;
                if (a.Recent) return recentRank(a) < recentRank(b);
                if (a.Entry->Category != b.Entry->Category) return CategoryOrder(a.Entry->Category) < CategoryOrder(b.Entry->Category);
                return false;
            });
            return results;
        }

    private:
        static int CategoryOrder(const std::string& category)
        {
            static const std::vector<std::string> order = {"Events", "Flow Control", "Math", "Constants", "Variables",
                                                           "Objects", "Entities", "Utility", "Reroute", kCombosCategory};
            const auto it = std::ranges::find(order, category);
            return static_cast<int>(it - order.begin());
        }

        std::vector<NodeEntry> m_Entries;
        std::unordered_map<std::string, std::string> m_Descriptions;
    };

    [[nodiscard]] inline std::vector<NodeEntry> VariableNodeEntries(const AnimationGraphData& graph)
    {
        std::vector<NodeEntry> entries;
        for (const auto& variable : graph.Variables)
        {
            entries.push_back(SingleNodeEntry("Get " + variable.Name, "Variables", "Reads the value of this variable.",
                                              {"variable", "get", variable.Name},
                                              [variable](const int id)
                                              {
                                                  return CreateVariableGetNode(id, variable.Name, variable.Type);
                                              }));
            entries.push_back(SingleNodeEntry("Set " + variable.Name, "Variables", "Assigns a value to this variable.",
                                              {"variable", "set", "assign", variable.Name},
                                              [variable](const int id)
                                              {
                                                  return CreateVariableSetNode(id, variable.Name, variable.Type);
                                              }));
        }
        return entries;
    }

    [[nodiscard]] inline NodeRegistry BuildNodeRegistry()
    {
        NodeRegistry registry;
        const auto add = [&](std::string name, std::string category, std::string description,
                             std::vector<std::string> keywords, std::function<Node(int)> make)
        {
            registry.Add(SingleNodeEntry(std::move(name), std::move(category), std::move(description), std::move(keywords),
                                         std::move(make)));
        };

        add("Start", "Events", "Runs once when play begins.", {"begin", "event", "init"}, CreateStartEventNode);
        add("Tick", "Events", "Runs every frame and provides the delta time.", {"update", "event", "frame"}, CreateTickEventNode);

        add("Branch", "Flow Control", "Continues down True or False depending on a condition.", {"if", "else", "condition"},
            CreateBranchNode);
        add("For Loop", "Flow Control", "Repeats the loop body for each index in a range.", {"loop", "iterate", "repeat"},
            CreateForNode);

        struct TypeOption { const char* Label; PinType Type; };
        static constexpr TypeOption kConstantTypes[] = {
            {"Bool", PinType::Bool}, {"Float", PinType::Float}, {"Int", PinType::Int}, {"Vec2", PinType::Vec2},
            {"Vec3", PinType::Vec3}, {"Vec4", PinType::Vec4}, {"String", PinType::String},
        };
        for (const auto& [label, type] : kConstantTypes)
        {
            add(std::string(label) + " Constant", "Constants", "A fixed value you edit directly on the node.",
                {"constant", "literal", "value", label}, [type](const int id) { return CreateConstantNode(id, type); });
        }

        struct MathOption { const char* Label; NodeSubType Op; const char* Description; };
        static constexpr MathOption kMath[] = {
            {"Add", NodeSubType::Add, "Adds two values."},
            {"Subtract", NodeSubType::Sub, "Subtracts B from A."},
            {"Multiply", NodeSubType::Mul, "Multiplies two values."},
            {"Divide", NodeSubType::Div, "Divides A by B."},
            {"Min", NodeSubType::Min, "The smaller of two values."},
            {"Max", NodeSubType::Max, "The larger of two values."},
            {"Negate", NodeSubType::Negate, "Flips the sign of a value."},
            {"Sin", NodeSubType::Sin, "Sine of an angle in radians."},
            {"Cos", NodeSubType::Cos, "Cosine of an angle in radians."},
            {"Tan", NodeSubType::Tan, "Tangent of an angle in radians."},
            {"Sqrt", NodeSubType::Sqrt, "Square root."},
            {"Length", NodeSubType::Length, "Length of a vector."},
            {"Distance", NodeSubType::Distance, "Distance between two points."},
            {"Lerp", NodeSubType::Lerp, "Linear interpolation between A and B."},
            {"Clamp", NodeSubType::Clamp, "Limits a value to a range."},
            {"Look At", NodeSubType::LookAt, "Rotation that faces a target position."},
            {"And", NodeSubType::And, "True when both inputs are true."},
            {"Or", NodeSubType::Or, "True when either input is true."},
        };
        for (const auto& [label, op, description] : kMath)
        {
            add(label, "Math", description, {"math", "operator"}, [op](const int id) { return CreateMathNode(id, op); });
        }

        for (const auto* category : GetPropertyCategories())
        {
            const std::string component = category->ComponentName;
            const std::string display = category->DisplayName;
            add("Get " + display, "Objects", "Reads " + display + " properties from an entity.", {"object", "entity", "read", component},
                [component](const int id) { return CreateGetterNode(id, component); });
            add("Decompose " + display, "Objects", "Splits " + display + " into its individual values.",
                {"object", "entity", "break", "split", component},
                [component](const int id) { return CreateDecomposerNode(id, component); });
            add("Set " + display, "Objects", "Writes " + display + " properties on an entity.", {"object", "entity", "write", component},
                [component](const int id) { return CreateSetterNode(id, component); });
        }

        add("Spawn Entity", "Entities", "Creates a new entity from a template.", {"create", "instantiate", "prefab"},
            CreateSpawnEntityNode);
        add("Clone Entity", "Entities", "Duplicates an existing entity.", {"copy", "duplicate"}, CreateCloneEntityNode);
        add("Destroy Entity", "Entities", "Removes an entity from the scene.", {"delete", "remove", "kill"},
            CreateDestroyEntityNode);

        registry.Add(NodeEntry{"Comment", "Utility", "A titled box that groups nodes; dragging it moves the nodes inside.",
                               {"group", "box", "note", "frame"},
                               [](AnimationGraphData& graph)
                               {
                                   Comment comment;
                                   comment.Id = graph.AllocateId();
                                   graph.Comments.push_back(comment);
                                   return std::vector<int>{comment.Id};
                               }});

        static constexpr std::pair<const char*, PinType> kRerouteTypes[] = {
            {"Flow", PinType::Flow}, {"Bool", PinType::Bool}, {"Float", PinType::Float}, {"Int", PinType::Int},
            {"Vec2", PinType::Vec2}, {"Vec3", PinType::Vec3}, {"Vec4", PinType::Vec4}, {"String", PinType::String},
            {"Object", PinType::Object},
        };
        for (const auto& [label, type] : kRerouteTypes)
        {
            add(std::string("Reroute (") + label + ")", "Reroute", "Passes a wire through unchanged; use it to tidy long links.",
                {"reroute", "knot", "wire", "passthrough"}, [type](const int id) { return CreateRerouteNode(id, type); });
        }

        add("Print", "Utility", "Logs a value to the console.", {"log", "debug", "output"}, CreatePrintNode);

        static constexpr TypeOption kVariableTypes[] = {
            {"Bool", PinType::Bool}, {"Float", PinType::Float}, {"Int", PinType::Int}, {"Vec2", PinType::Vec2},
            {"Vec3", PinType::Vec3}, {"Vec4", PinType::Vec4}, {"String", PinType::String}, {"Object", PinType::Object},
        };
        for (const auto& [label, type] : kVariableTypes)
        {
            add(std::string("Get Variable (") + label + ")", "Variables", "Reads a variable chosen in the details panel.",
                {"variable", "get"},
                [type, name = std::string("Get Variable (") + label + ")"](const int id)
                {
                    Node node = CreateVariableGetNode(id, "", type);
                    node.Name = name;
                    return node;
                });
            add(std::string("Set Variable (") + label + ")", "Variables", "Assigns a variable chosen in the details panel.",
                {"variable", "set"},
                [type, name = std::string("Set Variable (") + label + ")"](const int id)
                {
                    Node node = CreateVariableSetNode(id, "", type);
                    node.Name = name;
                    return node;
                });
        }
        return registry;
    }
}
