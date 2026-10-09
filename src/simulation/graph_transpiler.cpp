module MoleHole;

import :Simulation.GraphTranspiler;
import :Simulation.GraphIr;
import :Simulation.AnimationGraph;
import :Simulation.AnimationGraphProperties;
import :Simulation.SceneGraphs;
import std;
import glm;
import GPP;

namespace MoleHole
{
    namespace
    {
        constexpr int kMaxForIterations = 100000;
        constexpr std::size_t kTableThreshold = 120;

        struct Line
        {
            int Indent{0};
            std::string Text;
            int Node{0};
        };
        using Block = std::vector<Line>;

        void Add(Block& block, const int indent, std::string text, const int node)
        {
            block.push_back(Line{indent, std::move(text), node});
        }

        void Append(Block& dest, const Block& source, const int indent = 0)
        {
            for (const auto& line : source) dest.push_back(Line{line.Indent + indent, line.Text, line.Node});
        }

        std::string Quote(const std::string_view text)
        {
            std::string out = "\"";
            for (const char raw : text)
            {
                const auto c = static_cast<unsigned char>(raw);
                if (c == '\\' || c == '"') { out += '\\'; out += raw; }
                else if (c == '\n') out += "\\n";
                else if (c == '\r') out += "\\r";
                else if (c == '\t') out += "\\t";
                else if (c < 32 || c == 127) out += std::format("\\{:03}", static_cast<int>(c));
                else out += raw;
            }
            return out + "\"";
        }

        std::string Number(const float value)
        {
            if (std::isnan(value)) return "(0/0)";
            if (std::isinf(value)) return value > 0 ? "math.huge" : "(-math.huge)";
            return std::format("{}", value);
        }

        std::string Sanitize(const std::string& name)
        {
            std::string out;
            for (const char c : name) out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
            return out;
        }

        const char* TypeAlias(const PinType type)
        {
            switch (type)
            {
            case PinType::Bool: return "T_BOOL";
            case PinType::Float: return "T_FLOAT";
            case PinType::Int: return "T_INT";
            case PinType::Vec2: return "T_VEC2";
            case PinType::Vec3: return "T_VEC3";
            case PinType::Vec4: return "T_VEC4";
            case PinType::String: return "T_STRING";
            case PinType::Object: return "T_OBJECT";
            case PinType::Flow: default: return "T_FLOW";
            }
        }

        const char* MathHelper(const NodeSubType op)
        {
            switch (op)
            {
            case NodeSubType::Add: return "add";
            case NodeSubType::Sub: return "sub";
            case NodeSubType::Mul: return "mul";
            case NodeSubType::Div: return "div";
            case NodeSubType::Min: return "min";
            case NodeSubType::Max: return "max";
            case NodeSubType::Negate: return "neg";
            case NodeSubType::Sin: return "sin";
            case NodeSubType::Cos: return "cos";
            case NodeSubType::Tan: return "tan";
            case NodeSubType::Sqrt: return "sqrt";
            case NodeSubType::Length: return "length";
            case NodeSubType::Distance: return "distance";
            case NodeSubType::Lerp: return "lerp";
            case NodeSubType::Clamp: return "clamp";
            case NodeSubType::And: return "land";
            case NodeSubType::Or: return "lor";
            case NodeSubType::LookAt: return "lookat";
            default: return nullptr;
            }
        }

        struct FnState
        {
            const IrGraph* G{nullptr};
            int GraphIndex{0};
            bool PureFn{false};
            bool HasDone{false};
            bool ThunkTable{false};
            bool FlowTable{false};
            std::set<int> Needed;
            std::set<int> Shared;
            std::string Tail{"return"};
            std::size_t Results{0};
        };

        enum class FnKind { Start, Tick, Function };

        class Emitter
        {
        public:
            Emitter(const SceneGraphs& scene, IrCache& cache, const TranspileOptions& options)
                : m_Scene(scene), m_Cache(cache), m_Options(options)
            {
            }

            TranspiledScript Run(const NamedGraph& main)
            {
                TranspiledScript script;
                if (main.IsFunction)
                {
                    script.Error = "'" + main.Name + "' is a function, not an event graph";
                    return script;
                }
                m_Graphs.push_back(m_Cache.Get(main, &m_Scene));
                CollectFunctions(*m_Graphs[0]);

                Block body;
                Add(body, 0, "local M = {}", 0);
                Add(body, 0, "local vars = " + VariableTable(m_Graphs[0]->Variables), 0);
                if (m_Graphs.size() > 1)
                {
                    std::string names;
                    for (std::size_t i = 1; i < m_Graphs.size(); ++i) names += (i > 1 ? ", " : "") + FnName(i);
                    Add(body, 0, "local " + names, 0);
                    for (std::size_t i = 1; i < m_Graphs.size(); ++i) EmitFunction(body, i);
                }
                EmitEvent(body, FnKind::Start);
                EmitEvent(body, FnKind::Tick);
                Add(body, 0, "return M", 0);

                Block header = Header();
                Block all;
                Append(all, header);
                Append(all, body);
                for (const auto& graph : m_Graphs) script.Graphs.push_back(graph->Name);
                for (const auto& line : all)
                {
                    script.Source += std::string(static_cast<std::size_t>(line.Indent) * 4, ' ') + line.Text + "\n";
                    script.Map.Add(line.Node);
                }
                return script;
            }

        private:
            const SceneGraphs& m_Scene;
            IrCache& m_Cache;
            TranspileOptions m_Options;
            std::vector<std::shared_ptr<const IrGraph>> m_Graphs;
            std::map<std::string, std::size_t> m_FunctionIndex;
            std::set<std::string> m_UsedRt;
            std::set<std::string> m_UsedHost;
            std::set<PinType> m_UsedTypes;
            int m_CurrentGraph{0};

            // Source-map ids carry the graph so errors inside called functions point at the right graph.
            int MapId(const int node) const { return node == 0 ? 0 : m_CurrentGraph * kSourceMapGraphStride + node; }

            std::string Rt(const char* name)
            {
                m_UsedRt.insert(name);
                return name;
            }

            std::string Host(const char* name)
            {
                m_UsedHost.insert(name);
                return name;
            }

            std::string Ty(const PinType type)
            {
                m_UsedTypes.insert(type);
                return TypeAlias(type);
            }

            Block Header()
            {
                Block header;
                if (!m_UsedTypes.empty())
                {
                    std::string names, values;
                    for (const auto type : m_UsedTypes)
                    {
                        names += (names.empty() ? "" : ", ") + std::string(TypeAlias(type));
                        values += (values.empty() ? "" : ", ") + std::to_string(static_cast<int>(type));
                    }
                    Add(header, 0, "local " + names + " = " + values, 0);
                }
                const auto alias = [&](const std::set<std::string>& used, const char* table)
                {
                    if (used.empty()) return;
                    std::string names, values;
                    for (const auto& name : used)
                    {
                        names += (names.empty() ? "" : ", ") + name;
                        values += (values.empty() ? "" : ", ") + std::string(table) + "." + name;
                    }
                    Add(header, 0, "local " + names + " = " + values, 0);
                };
                alias(m_UsedRt, "rt");
                alias(m_UsedHost, "host");
                return header;
            }

            void CollectFunctions(const IrGraph& graph)
            {
                for (const auto& node : graph.Nodes)
                {
                    if (node.SubType != NodeSubType::FunctionCall || m_FunctionIndex.contains(node.Function)) continue;
                    const auto it = std::ranges::find_if(m_Scene.Items, [&](const NamedGraph& g)
                    {
                        return g.IsFunction && g.Name == node.Function;
                    });
                    if (it == m_Scene.Items.end()) continue;
                    m_Graphs.push_back(m_Cache.Get(*it, &m_Scene));
                    m_FunctionIndex[node.Function] = m_Graphs.size() - 1;
                    CollectFunctions(*m_Graphs.back());
                }
            }

            std::string FnName(const std::size_t index) const
            {
                return "fn_" + Sanitize(m_Graphs[index]->Name) + "_" + std::to_string(index);
            }

            // ---- literals and expressions ----------------------------------------------------

            std::string Literal(const Value& value)
            {
                return std::visit([&](const auto& v) -> std::string
                {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, std::monostate>) return "nil";
                    else if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
                    else if constexpr (std::is_same_v<T, int>) return std::to_string(v);
                    else if constexpr (std::is_same_v<T, float>) return Number(v);
                    else if constexpr (std::is_same_v<T, std::string>) return Quote(v);
                    else if constexpr (std::is_same_v<T, std::uint64_t>) return Host("ent") + "(\"" + std::to_string(v) + "\")";
                    else if constexpr (std::is_same_v<T, glm::vec2>) return Rt("V2") + "(" + Number(v.x) + ", " + Number(v.y) + ")";
                    else if constexpr (std::is_same_v<T, glm::vec3>)
                        return Rt("V3") + "(" + Number(v.x) + ", " + Number(v.y) + ", " + Number(v.z) + ")";
                    else
                        return Rt("V4") + "(" + Number(v.x) + ", " + Number(v.y) + ", " + Number(v.z) + ", " + Number(v.w) + ")";
                }, value);
            }

            std::string VariableTable(const std::vector<Variable>& variables)
            {
                std::string out = "{";
                bool first = true;
                for (const auto& variable : variables)
                {
                    if (std::holds_alternative<std::monostate>(variable.Default)) continue;
                    out += std::string(first ? "" : ", ") + "[" + Quote(variable.Name) + "] = " + Literal(variable.Default);
                    first = false;
                }
                return out + "}";
            }

            std::string ThunkName(const FnState& fn, const int node) const
            {
                return fn.ThunkTable ? "T[" + std::to_string(node) + "]" : "n" + std::to_string(node);
            }

            std::string FlowName(const FnState& fn, const int node) const
            {
                return fn.FlowTable ? "F[" + std::to_string(node) + "]" : "f" + std::to_string(node);
            }

            bool IsMulti(const IrNode& node) const
            {
                return node.Type == NodeType::Decomposer || node.Type == NodeType::Call;
            }

            bool Inlined(const IrNode& node) const
            {
                return m_Options.InlinePure && node.Stable && node.DataConsumers <= 1 && node.Type != NodeType::Variable &&
                       node.Type != NodeType::Call;
            }

            std::string PinExpr(FnState& fn, const IrNode& node, const std::size_t input)
            {
                if (input >= node.Inputs.size() || node.Inputs[input].Source < 0) return "nil";
                return OutExpr(fn, node.Inputs[input].Source, node.Inputs[input].SourcePin);
            }

            std::string OutExpr(FnState& fn, const int sourceIndex, const int pin)
            {
                const IrNode& source = fn.G->Nodes[sourceIndex];
                if (source.Type == NodeType::Event)
                {
                    if (source.SubType == NodeSubType::Tick && pin == 1) return "dt";
                    if (source.SubType == NodeSubType::FunctionEntry && source.OutputTypes[pin] != PinType::Flow)
                    {
                        int ordinal = 0;
                        for (int i = 0; i < pin; ++i) ordinal += source.OutputTypes[i] != PinType::Flow;
                        return "a" + std::to_string(ordinal + 1);
                    }
                    return "nil";
                }
                if (!source.Pure) return "c[" + std::to_string(source.OutputPinIds[pin]) + "]";
                if (source.Type == NodeType::Reroute) return PinExpr(fn, source, 0);
                if (source.Type == NodeType::Constant) return pin == 0 ? Literal(source.Constant) : "nil";
                const std::string suffix = IsMulti(source) ? "[" + std::to_string(pin + 1) + "]" : "";
                if (Inlined(source))
                {
                    const std::string expr = NodeExpr(fn, source);
                    return IsMulti(source) ? "(" + expr + ")" + suffix : expr;
                }
                fn.Needed.insert(sourceIndex);
                return ThunkName(fn, source.Id) + "()" + suffix;
            }

            std::string NodeExpr(FnState& fn, const IrNode& node)
            {
                switch (node.Type)
                {
                case NodeType::Function:
                {
                    const char* helper = MathHelper(node.SubType);
                    if (!helper) return "nil";
                    std::string args;
                    const std::size_t arity = node.SubType == NodeSubType::Lerp || node.SubType == NodeSubType::Clamp ? 3
                        : (node.SubType == NodeSubType::Sin || node.SubType == NodeSubType::Cos || node.SubType == NodeSubType::Tan ||
                           node.SubType == NodeSubType::Sqrt || node.SubType == NodeSubType::Negate ||
                           node.SubType == NodeSubType::Length) ? 1 : 2;
                    for (std::size_t i = 0; i < arity; ++i) args += (i ? ", " : "") + PinExpr(fn, node, i);
                    return Rt(helper) + "(" + args + ")";
                }
                case NodeType::Getter:
                    return Host("entity") + "(" + std::to_string(node.Id) +
                           (node.TargetGuid != 0 ? ", \"" + std::to_string(node.TargetGuid) + "\"" : "") + ")";
                case NodeType::Decomposer:
                    if (node.Inputs.empty()) return "{}";
                    return Host("decompose") + "(" + std::to_string(node.Id) + ", " + Quote(node.Component) + ", " +
                           PinExpr(fn, node, 0) + ")";
                case NodeType::Variable:
                    if (node.Variable.empty())
                    {
                        return "(" + Host("diag") + "(" + std::to_string(node.Id) + ", 2, \"No variable assigned\") or vars[\"\"])";
                    }
                    return "vars[" + Quote(node.Variable) + "]";
                case NodeType::Call:
                {
                    const auto it = m_FunctionIndex.find(node.Function);
                    if (it == m_FunctionIndex.end())
                    {
                        return "{" + Host("diag") + "(" + std::to_string(node.Id) + ", 2, " +
                               Quote("Function '" + node.Function + "' does not exist") + ")}";
                    }
                    return "{" + CallExpr(fn, node, it->second) + "}";
                }
                default: return "nil";
                }
            }

            std::string CallExpr(FnState& fn, const IrNode& node, const std::size_t functionIndex)
            {
                std::string args;
                for (std::size_t i = 0; i < node.Inputs.size(); ++i)
                {
                    if (node.Inputs[i].Type == PinType::Flow) continue;
                    args += ", " + PinExpr(fn, node, i);
                }
                return Rt("call") + "(" + std::to_string(node.Id) + ", " + Quote(node.Function) + ", " + FnName(functionIndex) + args + ")";
            }

            Block ThunkBlock(FnState& fn, const int index)
            {
                const IrNode& node = fn.G->Nodes[index];
                Block out;
                const int id = node.Id;
                Add(out, 0, ThunkName(fn, id) + " = " + Rt("lazy") + "(c, " + std::to_string(id) + ", function()", id);
                Add(out, 1, "local v = " + NodeExpr(fn, node), id);
                if (IsMulti(node))
                {
                    for (std::size_t i = 0; i < node.OutputPinIds.size(); ++i)
                    {
                        if (node.OutputTypes[i] == PinType::Flow) continue;
                        Add(out, 1, std::format("if v[{}] ~= nil then {}({}, {}, v[{}]) end", i + 1, Host("pin"),
                                                node.OutputPinIds[i], Ty(node.OutputTypes[i]), i + 1), id);
                    }
                }
                else if (!node.OutputPinIds.empty())
                {
                    Add(out, 1, std::format("{}({}, {}, v)", Host("pin"), node.OutputPinIds[0], Ty(node.OutputTypes[0])), id);
                    if (node.Type == NodeType::Function)
                    {
                        Add(out, 1, std::format("if v == nil then {}({}, 1, \"Inputs are unconnected or have mismatched types\") end",
                                                Host("diag"), id), id);
                    }
                }
                Add(out, 1, "return v", id);
                Add(out, 0, "end)", id);
                return out;
            }

            // ---- flow ----------------------------------------------------------------------------

            Block FlowFrom(FnState& fn, const int index, const std::size_t pin)
            {
                Block out;
                const IrNode& node = fn.G->Nodes[index];
                if (pin >= node.Flow.size()) return out;
                for (const auto& edge : node.Flow[pin])
                {
                    if (edge.Cut)
                    {
                        Add(out, 0, std::format("{}({}, 2, \"Flow loop: depth limit reached\")", Host("diag"), node.Id), node.Id);
                        continue;
                    }
                    Add(out, 0, std::format("if {}({}) then", Host("link"), edge.LinkId), node.Id);
                    if (fn.Shared.contains(edge.Target))
                    {
                        Add(out, 1, FlowName(fn, fn.G->Nodes[edge.Target].Id) + "()", fn.G->Nodes[edge.Target].Id);
                        if (fn.HasDone) Add(out, 1, "if done then " + fn.Tail + " end", node.Id);
                    }
                    else
                    {
                        Append(out, ExecNode(fn, edge.Target), 1);
                    }
                    Add(out, 0, "end", node.Id);
                }
                return out;
            }

            PinType PrintType(const FnState& fn, const IrInput& input) const
            {
                if (input.Source >= 0 && fn.G->Nodes[input.Source].SubType == NodeSubType::For && input.SourcePin == 1)
                {
                    return PinType::Float;
                }
                return input.Type;
            }

            Block ExecNode(FnState& fn, const int index)
            {
                const IrNode& node = fn.G->Nodes[index];
                const int id = node.Id;
                Block out;
                Add(out, 0, std::format("if not {}({}) then return end", Host("enter"), id), id);
                const auto flow = [&](const std::size_t pin) { Append(out, FlowFrom(fn, index, pin)); };
                switch (node.Type)
                {
                case NodeType::Print:
                    if (node.Inputs.size() >= 2)
                    {
                        Add(out, 0, std::format("{}({}({}, {}))", Host("log"), Host("tostr"), PinExpr(fn, node, 1),
                                                Ty(PrintType(fn, node.Inputs[1]))), id);
                    }
                    flow(0);
                    break;
                case NodeType::Reroute: flow(0); break;
                case NodeType::Control: ExecControl(fn, index, out); break;
                case NodeType::Call: ExecCall(fn, index, out); break;
                case NodeType::Entity: ExecEntity(fn, index, out); flow(0); break;
                case NodeType::Setter: ExecSetter(fn, index, out); flow(0); break;
                case NodeType::Variable:
                    if (node.SubType == NodeSubType::VariableSet)
                    {
                        if (node.Inputs.size() >= 2)
                        {
                            if (node.Variable.empty())
                            {
                                Add(out, 0, std::format("{}({}, 2, \"No variable assigned\")", Host("diag"), id), id);
                            }
                            Add(out, 0, "vars[" + Quote(node.Variable) + "] = " + PinExpr(fn, node, 1), id);
                        }
                        flow(0);
                    }
                    break;
                default: break;
                }
                return out;
            }

            void ExecControl(FnState& fn, const int index, Block& out)
            {
                const IrNode& node = fn.G->Nodes[index];
                const int id = node.Id;
                if (node.SubType == NodeSubType::FunctionReturn)
                {
                    std::size_t slot = 0;
                    for (std::size_t i = 0; i < node.Inputs.size(); ++i)
                    {
                        if (node.Inputs[i].Type == PinType::Flow) continue;
                        if (slot < fn.Results && node.Inputs[i].Source >= 0)
                        {
                            Add(out, 0, "do local v = " + PinExpr(fn, node, i) + "; if v ~= nil then r" + std::to_string(slot + 1) + " = v end end", id);
                        }
                        ++slot;
                    }
                    Add(out, 0, "done = true", id);
                    Add(out, 0, fn.Tail, id);
                }
                else if (node.SubType == NodeSubType::Branch)
                {
                    if (node.Inputs.size() < 2) return;
                    Block yes = FlowFrom(fn, index, 0);
                    Block no = node.OutputPinIds.size() > 1 ? FlowFrom(fn, index, 1) : Block{};
                    if (yes.empty() && no.empty()) return;
                    Add(out, 0, "if " + Rt("bool") + "(" + PinExpr(fn, node, 1) + ") then", id);
                    Append(out, yes, 1);
                    if (!no.empty())
                    {
                        Add(out, 0, "else", id);
                        Append(out, no, 1);
                    }
                    Add(out, 0, "end", id);
                }
                else if (node.SubType == NodeSubType::For)
                {
                    if (node.Inputs.size() < 3) return;
                    const std::string i = std::format("i{}", id), n = std::format("n{}", id);
                    Add(out, 0, std::format("local s{0}, e{0} = {1}({2}), {1}({3})", id, Rt("int"), PinExpr(fn, node, 1),
                                            PinExpr(fn, node, 2)), id);
                    Add(out, 0, "local " + n + " = 0", id);
                    Add(out, 0, std::format("for {0} = s{1}, e{1} - 1 do", i, id), id);
                    Add(out, 1, std::format("if not {}() then break end", Host("alive")), id);
                    Add(out, 1, n + " += 1", id);
                    Add(out, 1, std::format("if {} > {} then", n, kMaxForIterations), id);
                    Add(out, 2, std::format("{}(\"[GraphExecutor] For loop aborted: exceeded max iteration cap\")", Host("log")), id);
                    Add(out, 2, std::format("{}({}, 2, \"Loop aborted: iteration cap exceeded\")", Host("diag"), id), id);
                    Add(out, 2, "break", id);
                    Add(out, 1, "end", id);
                    Add(out, 1, Rt("clear") + "(c)", id);
                    if (node.OutputPinIds.size() > 1)
                    {
                        Add(out, 1, std::format("{}(c, {}, {}, {})", Rt("setp"), node.OutputPinIds[1], Ty(PinType::Float), i), id);
                    }
                    Append(out, FlowFrom(fn, index, 0), 1);
                    Add(out, 0, "end", id);
                    if (node.OutputPinIds.size() > 2) Append(out, FlowFrom(fn, index, 2));
                }
            }

            void ExecCall(FnState& fn, const int index, Block& out)
            {
                const IrNode& node = fn.G->Nodes[index];
                const int id = node.Id;
                const auto it = m_FunctionIndex.find(node.Function);
                if (it == m_FunctionIndex.end())
                {
                    Add(out, 0, std::format("{}({}, 2, {})", Host("diag"), id,
                                            Quote("Function '" + node.Function + "' does not exist")), id);
                }
                else
                {
                    std::vector<std::size_t> dataPins;
                    for (std::size_t i = 0; i < node.OutputPinIds.size(); ++i)
                    {
                        if (node.OutputTypes[i] != PinType::Flow) dataPins.push_back(i);
                    }
                    std::string results;
                    for (std::size_t k = 0; k < dataPins.size(); ++k) results += (k ? ", " : "") + std::format("r{}", k + 1);
                    const std::string call = CallExpr(fn, node, it->second);
                    Add(out, 0, results.empty() ? call : "local " + results + " = " + call, id);
                    for (std::size_t k = 0; k < dataPins.size(); ++k)
                    {
                        Add(out, 0, std::format("{}(c, {}, {}, r{})", Rt("setp"), node.OutputPinIds[dataPins[k]],
                                                Ty(node.OutputTypes[dataPins[k]]), k + 1), id);
                    }
                }
                if (!node.OutputTypes.empty() && node.OutputTypes[0] == PinType::Flow) Append(out, FlowFrom(fn, index, 0));
            }

            void ExecEntity(FnState& fn, const int index, Block& out)
            {
                const IrNode& node = fn.G->Nodes[index];
                const int id = node.Id;
                const auto produce = [&](const std::string& call)
                {
                    if (node.OutputPinIds.size() > 1)
                    {
                        Add(out, 0, "local g = " + call, id);
                        Add(out, 0, std::format("if g ~= nil then {}(c, {}, {}, g) end", Rt("setp"), node.OutputPinIds[1],
                                                Ty(PinType::Object)), id);
                    }
                    else
                    {
                        Add(out, 0, call, id);
                    }
                };
                if (node.SubType == NodeSubType::SpawnEntity && node.Inputs.size() >= 4)
                {
                    produce(std::format("{}({}, {}, {}, {})", Host("spawn"), id, PinExpr(fn, node, 1), PinExpr(fn, node, 2),
                                        PinExpr(fn, node, 3)));
                }
                else if (node.SubType == NodeSubType::DestroyEntity && node.Inputs.size() >= 2)
                {
                    Add(out, 0, std::format("{}({}, {})", Host("destroy"), id, PinExpr(fn, node, 1)), id);
                }
                else if (node.SubType == NodeSubType::CloneEntity && node.Inputs.size() >= 3)
                {
                    produce(std::format("{}({}, {}, {})", Host("clone"), id, PinExpr(fn, node, 1), PinExpr(fn, node, 2)));
                }
            }

            void ExecSetter(FnState& fn, const int index, Block& out)
            {
                const IrNode& node = fn.G->Nodes[index];
                const int id = node.Id;
                if (node.Inputs.size() < 2) return;
                const std::string e = std::format("e{}", id);
                Add(out, 0, "local " + e + " = " + PinExpr(fn, node, 1), id);
                Add(out, 0, std::format("if {}({}, {}, {}) then", Host("setter"), id, Quote(node.Component), e), id);
                if (const PropertyCategory* category = FindPropertyCategory(node.Component))
                {
                    for (std::size_t i = 2; i < node.Inputs.size(); ++i)
                    {
                        const std::size_t property = i - 2;
                        if (property >= category->Properties.size()) break;
                        if (node.Inputs[i].Source < 0) continue;
                        Add(out, 1, std::format("{}({}, {}, {}, {})", Host("setfield"), Quote(node.Component), e,
                                                Quote(category->Properties[property].Label), PinExpr(fn, node, i)), id);
                    }
                }
                if (node.OutputPinIds.size() > 1)
                {
                    Add(out, 1, std::format("{}(c, {}, {}, {})", Rt("setp"), node.OutputPinIds[1], Ty(PinType::Object), e), id);
                }
                Add(out, 0, "end", id);
            }

            // ---- functions -----------------------------------------------------------------------

            void Reach(FnState& fn, const std::vector<int>& roots, std::vector<int>& postOrder)
            {
                std::set<int> seen;
                std::map<int, int> incoming;
                const auto visit = [&](auto&& self, const int index) -> void
                {
                    seen.insert(index);
                    for (const auto& edges : fn.G->Nodes[index].Flow)
                    {
                        for (const auto& edge : edges)
                        {
                            if (edge.Cut) continue;
                            ++incoming[edge.Target];
                            if (!seen.contains(edge.Target)) self(self, edge.Target);
                        }
                    }
                    postOrder.push_back(index);
                };
                for (const int root : roots) if (!seen.contains(root)) visit(visit, root);
                for (const auto& [index, count] : incoming)
                {
                    if (count >= 2 && !std::ranges::contains(roots, index)) fn.Shared.insert(index);
                }
                for (const int index : seen)
                {
                    if (fn.G->Nodes[index].SubType == NodeSubType::FunctionReturn) fn.HasDone = !fn.PureFn;
                }
                fn.ThunkTable = fn.G->PureOrder.size() > kTableThreshold;
                fn.FlowTable = fn.Shared.size() > kTableThreshold;
            }

            // Assembles thunk definitions (dependencies first), shared flow functions and the root code.
            void Assemble(Block& out, FnState& fn, const std::vector<int>& postOrder, const Block& root, const int indent)
            {
                std::vector<Block> shared;
                for (const int index : postOrder)
                {
                    if (!fn.Shared.contains(index)) continue;
                    Block def;
                    const int id = fn.G->Nodes[index].Id;
                    const std::string name = FlowName(fn, id);
                    Add(def, 0, fn.FlowTable ? name + " = function()" : "local function " + name + "()", id);
                    Append(def, ExecNode(fn, index), 1);
                    Add(def, 0, "end", id);
                    shared.push_back(std::move(def));
                }
                std::map<int, Block> thunks;
                for (auto it = fn.G->PureOrder.rbegin(); it != fn.G->PureOrder.rend(); ++it)
                {
                    if (!fn.Needed.contains(*it)) continue;
                    thunks[*it] = ThunkBlock(fn, *it);
                }
                if (fn.ThunkTable) Add(out, indent, "local T = {}", 0);
                if (fn.FlowTable) Add(out, indent, "local F = {}", 0);
                for (const int index : fn.G->PureOrder)
                {
                    const auto it = thunks.find(index);
                    if (it == thunks.end()) continue;
                    Block block = it->second;
                    if (!fn.ThunkTable) block.front().Text = "local " + block.front().Text;
                    Append(out, block, indent);
                }
                for (const auto& def : shared) Append(out, def, indent);
                Append(out, root, indent);
            }

            void EmitEvent(Block& out, const FnKind kind)
            {
                FnState fn;
                fn.G = m_Graphs[0].get();
                fn.GraphIndex = 0;
                m_CurrentGraph = 0;
                const auto& roots = kind == FnKind::Start ? fn.G->StartEvents : fn.G->TickEvents;
                std::vector<int> post;
                Reach(fn, roots, post);
                Block root;
                for (const int index : roots)
                {
                    const IrNode& node = fn.G->Nodes[index];
                    Add(root, 0, std::format("if {}({}) then", Host("enter"), node.Id), node.Id);
                    if (kind == FnKind::Tick)
                    {
                        Add(root, 1, std::format("{}({}, {}, dt)", Host("pin"), node.OutputPinIds[1], Ty(PinType::Float)), node.Id);
                    }
                    Append(root, FlowFrom(fn, index, 0), 1);
                    Add(root, 0, "end", node.Id);
                }
                Block inner;
                Add(inner, 0, Rt("reset") + "()", 0);
                Add(inner, 0, "local c = {}", 0);
                if (fn.HasDone) Add(inner, 0, "local done = false", 0);
                Assemble(inner, fn, post, root, 0);
                for (auto& line : inner) line.Node = MapId(line.Node);
                Add(out, 0, kind == FnKind::Start ? "function M.start()" : "function M.tick(dt)", 0);
                Append(out, inner, 1);
                Add(out, 0, "end", 0);
            }

            void EmitFunction(Block& out, const std::size_t index)
            {
                FnState fn;
                fn.G = m_Graphs[index].get();
                fn.GraphIndex = static_cast<int>(index);
                m_CurrentGraph = fn.GraphIndex;
                const IrGraph& g = *fn.G;
                fn.PureFn = g.Signature.Pure;
                fn.Results = g.Signature.Outputs.size();
                if (fn.Results > 0)
                {
                    fn.Tail = "return";
                    for (std::size_t i = 0; i < fn.Results; ++i) fn.Tail += (i ? ", r" : " r") + std::to_string(i + 1);
                }

                std::string params;
                if (g.Entry >= 0)
                {
                    int ordinal = 0;
                    for (const auto type : g.Nodes[g.Entry].OutputTypes)
                    {
                        if (type == PinType::Flow) continue;
                        ++ordinal;
                        params += (ordinal > 1 ? ", a" : "a") + std::to_string(ordinal);
                    }
                }
                std::vector<int> post;
                Block root;
                Block inner;
                Add(inner, 0, "local c = {}", 0);
                Add(inner, 0, "local vars = " + VariableTable(g.Variables), 0);
                if (g.Entry < 0)
                {
                    Reach(fn, {}, post);
                    Add(root, 0, DefaultReturn(g), 0);
                }
                else if (fn.PureFn)
                {
                    Reach(fn, {}, post);
                    std::string results;
                    std::vector<std::size_t> dataInputs;
                    if (g.Return >= 0)
                    {
                        for (std::size_t i = 0; i < g.Nodes[g.Return].Inputs.size(); ++i)
                        {
                            if (g.Nodes[g.Return].Inputs[i].Type != PinType::Flow) dataInputs.push_back(i);
                        }
                    }
                    for (std::size_t slot = 0; slot < fn.Results; ++slot)
                    {
                        results += (slot ? ", " : "") +
                                   (g.Return >= 0 && slot < dataInputs.size() ? PinExpr(fn, g.Nodes[g.Return], dataInputs[slot])
                                                                               : Literal(DefaultValueFor(g.Signature.Outputs[slot].Type)));
                    }
                    Add(root, 0, results.empty() ? "return" : "return " + results, 0);
                }
                else
                {
                    Reach(fn, {g.Entry}, post);
                    std::string defaults;
                    for (std::size_t i = 0; i < fn.Results; ++i)
                    {
                        defaults += (i ? ", " : "") + Literal(DefaultValueFor(g.Signature.Outputs[i].Type));
                    }
                    std::string names;
                    for (std::size_t i = 0; i < fn.Results; ++i) names += (i ? ", r" : "r") + std::to_string(i + 1);
                    if (fn.Results > 0) Add(inner, 0, "local " + names + " = " + defaults, 0);
                    if (fn.HasDone) Add(inner, 0, "local done = false", 0);
                    if (!g.Nodes[g.Entry].OutputTypes.empty() && g.Nodes[g.Entry].OutputTypes[0] == PinType::Flow)
                    {
                        Append(root, FlowFrom(fn, g.Entry, 0));
                    }
                    Add(root, 0, fn.Tail, 0);
                }
                Assemble(inner, fn, post, root, 0);
                for (auto& line : inner) line.Node = MapId(line.Node);
                Add(out, 0, "function " + FnName(index) + "(" + params + ")", 0);
                Append(out, inner, 1);
                Add(out, 0, "end", 0);
                m_CurrentGraph = 0;
            }

            std::string DefaultReturn(const IrGraph& g)
            {
                std::string results;
                for (std::size_t i = 0; i < g.Signature.Outputs.size(); ++i)
                {
                    results += (i ? ", " : "") + Literal(DefaultValueFor(g.Signature.Outputs[i].Type));
                }
                return results.empty() ? "return" : "return " + results;
            }
        };
    }

    SourceLocation TranspiledScript::Locate(const int line) const
    {
        const int id = Map.Locate(line);
        if (id == 0) return {};
        const auto graph = static_cast<std::size_t>(id / kSourceMapGraphStride);
        return SourceLocation{graph < Graphs.size() ? Graphs[graph] : std::string{}, id % kSourceMapGraphStride};
    }

    TranspiledScript TranspileGraph(const SceneGraphs& graphs, const NamedGraph& main, IrCache& cache,
                                    const TranspileOptions& options)
    {
        Emitter emitter(graphs, cache, options);
        return emitter.Run(main);
    }

    std::shared_ptr<const TranspiledScript> ScriptCache::Get(const SceneGraphs& graphs, const NamedGraph& main,
                                                             const TranspileOptions& options)
    {
        const auto ir = m_Ir.Get(main, &graphs);
        std::uint64_t key = ir->Hash * 31 + (options.InlinePure ? 1 : 0);
        std::vector<std::string> pending;
        std::set<std::string> seen{main.Name};
        const auto walk = [&](auto&& self, const IrGraph& graph) -> void
        {
            for (const auto& node : graph.Nodes)
            {
                if (node.SubType != NodeSubType::FunctionCall || !seen.insert(node.Function).second) continue;
                const auto it = std::ranges::find_if(graphs.Items, [&](const NamedGraph& g)
                {
                    return g.IsFunction && g.Name == node.Function;
                });
                if (it == graphs.Items.end()) continue;
                const auto callee = m_Ir.Get(*it, &graphs);
                key = key * 1099511628211ull + callee->Hash;
                self(self, *callee);
            }
        };
        walk(walk, *ir);

        auto& entry = m_Entries[main.Name];
        if (!entry.Script || entry.Key != key)
        {
            entry.Script = std::make_shared<const TranspiledScript>(TranspileGraph(graphs, main, m_Ir, options));
            entry.Key = key;
            ++m_Transpilations;
        }
        return entry.Script;
    }
}

namespace MoleHole
{
    std::string_view GraphRuntimePrelude()
    {
        return R"lua(
local host = host
local rt = {}
local NIL = {}
local M2, M4 = vec_meta.vec2, vec_meta.vec4

function rt.V2(x, y) return vec2(x, y) end
rt.V3 = vector.create
function rt.V4(x, y, z, w) return vec4(x, y, z, w) end
rt.clear = table.clear

local function kind(v)
    local t = type(v)
    if t == "number" then return 1 end
    if t == "vector" then return 3 end
    if t == "table" then
        local m = getmetatable(v)
        if m == M2 then return 2 elseif m == M4 then return 4 end
    end
    return 0
end

local function make(k, x, y, z, w)
    if k == 2 then return vec2(x, y) elseif k == 3 then return vector.create(x, y, z) end
    return vec4(x, y, z, w)
end

local function zip(f, scalar)
    scalar = scalar or f
    return function(a, b)
        local k = kind(a)
        if k == 0 or k ~= kind(b) then return nil end
        if k == 1 then return scalar(a, b) end
        if k == 2 then return make(2, f(a.x, b.x), f(a.y, b.y)) end
        if k == 3 then return make(3, f(a.x, b.x), f(a.y, b.y), f(a.z, b.z)) end
        return make(4, f(a.x, b.x), f(a.y, b.y), f(a.z, b.z), f(a.w, b.w))
    end
end

local function map(f)
    return function(a)
        local k = kind(a)
        if k == 0 then return nil end
        if k == 1 then return f(a) end
        if k == 2 then return make(2, f(a.x), f(a.y)) end
        if k == 3 then return make(3, f(a.x), f(a.y), f(a.z)) end
        return make(4, f(a.x), f(a.y), f(a.z), f(a.w))
    end
end

rt.add = zip(function(a, b) return a + b end)
rt.sub = zip(function(a, b) return a - b end)
rt.mul = zip(function(a, b) return a * b end)
rt.div = zip(function(a, b) return a / b end, function(a, b) if b ~= 0 then return a / b end return 0 end)
rt.min = zip(math.min)
rt.max = zip(math.max)
rt.neg = map(function(a) return -a end)
rt.sqrt = map(function(a) return math.sqrt(math.max(0, a)) end)

local function number1(f)
    return function(a) if type(a) == "number" then return f(a) end return nil end
end
rt.sin = number1(math.sin)
rt.cos = number1(math.cos)
rt.tan = number1(math.tan)

local function vlength(a)
    local k = kind(a)
    if k == 2 then return math.sqrt(a.x * a.x + a.y * a.y) end
    if k == 3 then return math.sqrt(a.x * a.x + a.y * a.y + a.z * a.z) end
    if k == 4 then return math.sqrt(a.x * a.x + a.y * a.y + a.z * a.z + a.w * a.w) end
    return nil
end
rt.length = vlength
function rt.distance(a, b)
    local d = rt.sub(a, b)
    if d == nil then return nil end
    return vlength(d)
end

function rt.lerp(a, b, t)
    if type(t) ~= "number" then t = 0 end
    return zip(function(x, y) return x * (1 - t) + y * t end)(a, b)
end

function rt.clamp(v, lo, hi)
    local k = kind(v)
    if k == 0 or k ~= kind(lo) or k ~= kind(hi) then return nil end
    local function c(x, l, h) return math.min(math.max(x, l), h) end
    if k == 1 then return c(v, lo, hi) end
    if k == 2 then return make(2, c(v.x, lo.x, hi.x), c(v.y, lo.y, hi.y)) end
    if k == 3 then return make(3, c(v.x, lo.x, hi.x), c(v.y, lo.y, hi.y), c(v.z, lo.z, hi.z)) end
    return make(4, c(v.x, lo.x, hi.x), c(v.y, lo.y, hi.y), c(v.z, lo.z, hi.z), c(v.w, lo.w, hi.w))
end

function rt.bool(v)
    local t = type(v)
    if t == "boolean" then return v end
    if t == "number" then return v ~= 0 end
    return false
end

function rt.int(v)
    if type(v) ~= "number" then return 0 end
    if v >= 0 then return math.floor(v) end
    return math.ceil(v)
end

function rt.land(a, b) return rt.bool(a) and rt.bool(b) end
function rt.lor(a, b) return rt.bool(a) or rt.bool(b) end

function rt.lookat(a, b)
    if type(a) == "vector" and type(b) == "vector" then return host.lookat(a, b) end
    return nil
end

function rt.lazy(c, key, fn)
    return function()
        local v = c[key]
        if v == nil then
            v = fn()
            if v == nil then v = NIL end
            c[key] = v
        end
        if v == NIL then return nil end
        return v
    end
end

function rt.setp(c, pin, ty, v)
    c[pin] = v
    host.pin(pin, ty, v)
    return v
end

local stack = {}
function rt.reset() table.clear(stack) end

local function leave(...)
    stack[#stack] = nil
    host.pop()
    return ...
end

function rt.call(id, name, fn, ...)
    for i = 1, #stack do
        if stack[i] == name then
            host.diag(id, 1, "Recursive call to '" .. name .. "' skipped")
            return
        end
    end
    if #stack >= 16 then
        host.diag(id, 1, "Call depth limit reached")
        return
    end
    stack[#stack + 1] = name
    host.push(name)
    return leave(fn(...))
end

_G.rt = rt
)lua";
    }
}
