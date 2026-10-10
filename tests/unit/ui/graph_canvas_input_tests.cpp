#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#define private public
#include "../../../plugins/animation_graph_window_layer.cpp"
#undef private

namespace
{
    constexpr ImVec2 kPanDelta(-120.0f, -40.0f);
    constexpr ImVec2 kDragDelta(80.0f, 50.0f);

    struct CanvasRig
    {
        std::filesystem::path PreviousDirectory = std::filesystem::current_path();
        std::filesystem::path WorkDirectory = std::filesystem::temp_directory_path() / "molehole_graph_canvas_tests";
        ImGuiContext* Ctx = nullptr;
        std::shared_ptr<GPP::Logger> Log = std::make_shared<GPP::Logger>();
        std::shared_ptr<GPP::SceneManager> Scenes;
        std::shared_ptr<GPP::SimulationRunner> Runner;
        std::shared_ptr<UiState> State = std::make_shared<UiState>();
        std::shared_ptr<GPP::AssetOptions> Options = std::make_shared<GPP::AssetOptions>();
        std::shared_ptr<GPP::AssetDirectories> Assets;
        std::shared_ptr<GPP::EventDispatcher> Dispatcher = std::make_shared<GPP::EventDispatcher>();
        std::unique_ptr<AnimationGraphWindowLayer> Layer;
        bool DockBuilt = false;

        explicit CanvasRig(const std::function<void(AnimationGraphData&)>& populate = {})
        {
            std::filesystem::create_directories(WorkDirectory);
            std::filesystem::remove(WorkDirectory / "NodeEditor.json");
            std::filesystem::current_path(WorkDirectory);

            RegisterComponents();
            Ctx = ImGui::CreateContext();
            ImGui::SetCurrentContext(Ctx);
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2(1920, 1080);
            io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            unsigned char* pixels = nullptr;
            int w = 0, h = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);

            NamedGraph named;
            named.Name = "Main";
            auto& graph = named.Graph;
            if (populate) populate(graph);
            else DefaultGraph(graph);
            SceneGraphs graphs;
            graphs.Items.push_back(std::move(named));
            GPP::Scene scene("CanvasScene");
            StoreSceneGraphs(scene, graphs);

            Scenes = std::make_shared<GPP::SceneManager>(Log, std::make_shared<GPP::FileSystem>());
            Runner = Scenes->CreateSimulation(std::move(scene), std::vector<std::shared_ptr<GPP::ISimulationModule>>{});
            Runner->Start();
            Runner->SetPaused(true);
            Assets = std::make_shared<GPP::AssetDirectories>(Options);
            State->CurrentSceneName = "CanvasScene";
            State->ShowAnimationGraphWindow = true;
            Layer = std::make_unique<AnimationGraphWindowLayer>(Log, Scenes, State, Assets, Options, Dispatcher);
            Layer->OnAttach();
            Layer->OnUpdate(0.016f);
        }

        ~CanvasRig()
        {
            Layer->OnDetach();
            Layer.reset();
            ImGui::DestroyContext(Ctx);
            std::filesystem::current_path(PreviousDirectory);
        }

        static void DefaultGraph(AnimationGraphData& graph)
        {
            Node start = CreateStartEventNode(graph.AllocateId());
            start.Position = {40.0f, 60.0f};
            graph.Nodes.push_back(start);
            Node constant = CreateConstantNode(graph.AllocateId(), PinType::Float);
            constant.Position = {40.0f, 220.0f};
            graph.Nodes.push_back(constant);
            Node print = CreatePrintNode(graph.AllocateId());
            print.Position = {380.0f, 60.0f};
            graph.Nodes.push_back(print);
            graph.Links.push_back(Link{graph.AllocateId(), graph.Nodes[0].Outputs[0].Id, graph.Nodes[2].Inputs[0].Id});
            Node reroute = CreateRerouteNode(graph.AllocateId(), PinType::String);
            reroute.Position = {240.0f, 260.0f};
            graph.Nodes.push_back(reroute);
            Comment comment;
            comment.Id = graph.AllocateId();
            comment.Position = {20.0f, 20.0f};
            comment.Size = {520.0f, 380.0f};
            graph.Comments.push_back(comment);
        }

        void Frame()
        {
            ImGuiIO& io = ImGui::GetIO();
            io.DeltaTime = 1.0f / 60.0f;
            Layer->OnUpdate(io.DeltaTime);
            ImGui::NewFrame();
            const ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
            if (!DockBuilt)
            {
                DockBuilt = true;
                ImGui::DockBuilderRemoveNode(dock);
                ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_PassthruCentralNode);
                ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->Size);
                ImGuiID left, right;
                ImGui::DockBuilderSplitNode(dock, ImGuiDir_Right, 0.5f, &right, &left);
                ImGui::DockBuilderDockWindow("Viewport", left);
                ImGui::DockBuilderDockWindow("Animation Graph", right);
                ImGui::DockBuilderFinish(dock);
            }
            if (ImGui::Begin("Viewport")) ImGui::InvisibleButton("##viewport", ImGui::GetContentRegionAvail());
            ImGui::End();
            Layer->OnUiRender();
            ImGui::Render();
        }

        void Run(int frames) { for (int i = 0; i < frames; ++i) Frame(); }

        void Settle()
        {
            ImVec2 last(-1.0f, -1.0f);
            int stable = 0;
            for (int i = 0; i < 3000 && stable < 30; ++i)
            {
                Frame();
                const ImVec2 origin = CanvasOrigin();
                stable = (origin.x == last.x && origin.y == last.y) ? stable + 1 : 0;
                last = origin;
            }
        }

        std::string Probe() const
        {
            const ImGuiContext& g = *ImGui::GetCurrentContext();
            return std::string("nav=") + (g.NavWindow ? g.NavWindow->Name : "-") + " hovered=" +
                   (g.HoveredWindow ? g.HoveredWindow->Name : "-") + " active=" + std::to_string(g.ActiveId) +
                   " editorFocused=" + std::to_string(Layer->m_EditorFocused);
        }

        ImRect PanelRect() const
        {
            for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
                if (std::string_view(window->Name).contains("AnimationGraphEditorPanel")) return window->InnerRect;
            return {};
        }

        ImRect NodeScreenRect(int id) const
        {
            ed::SetCurrentEditor(Layer->m_EditorContext);
            const ImVec2 size = ed::GetNodeSize(ed::NodeId(id));
            const ImVec2 pos = ed::GetNodePosition(ed::NodeId(id));
            const ImRect rect(ed::CanvasToScreen(pos), ed::CanvasToScreen(ImVec2(pos.x + size.x, pos.y + size.y)));
            ed::SetCurrentEditor(nullptr);
            return rect;
        }

        ImVec2 CanvasOrigin() const
        {
            ed::SetCurrentEditor(Layer->m_EditorContext);
            const ImVec2 origin = ed::CanvasToScreen(ImVec2(0.0f, 0.0f));
            ed::SetCurrentEditor(nullptr);
            return origin;
        }

        ImVec2 EmptySpot() const
        {
            const ImRect panel = PanelRect();
            std::vector<ImRect> used;
            for (const auto& node : Layer->CurrentGraph().Nodes) used.push_back(NodeScreenRect(node.Id));
            for (const auto& comment : Layer->CurrentGraph().Comments) used.push_back(NodeScreenRect(comment.Id));
            for (float y = panel.Min.y + 30.0f; y < panel.Max.y - 30.0f; y += 15.0f)
                for (float x = panel.Min.x + 30.0f; x < panel.Max.x - 30.0f; x += 15.0f)
                {
                    const ImVec2 point(x, y);
                    const bool free = std::ranges::none_of(used, [&](ImRect rect)
                    {
                        rect.Expand(12.0f);
                        return rect.Contains(point);
                    });
                    if (free) return point;
                }
            return panel.GetCenter();
        }

        ImVec2 HeaderPoint(int id) const
        {
            const ImRect rect = NodeScreenRect(id);
            return ImVec2(rect.GetCenter().x, rect.Min.y + std::min(10.0f, rect.GetHeight() * 0.5f));
        }

        void MoveTo(ImVec2 point, int frames = 3)
        {
            ImGui::GetIO().AddMousePosEvent(point.x, point.y);
            Run(frames);
        }

        void Press(int button)
        {
            ImGui::GetIO().AddMouseButtonEvent(button, true);
            Run(3);
        }

        void Release(int button)
        {
            ImGui::GetIO().AddMouseButtonEvent(button, false);
            Run(5);
        }

        void Click(int button, ImVec2 point)
        {
            MoveTo(point);
            Press(button);
            Release(button);
        }

        void DragMouse(int button, ImVec2 from, ImVec2 delta)
        {
            MoveTo(from);
            Press(button);
            for (int i = 1; i <= 10; ++i) MoveTo(ImVec2(from.x + delta.x * i / 10.0f, from.y + delta.y * i / 10.0f), 1);
            Release(button);
        }

        float DragNodeBy(int id, ImVec2 delta)
        {
            const float before = NodeScreenRect(id).Min.x;
            DragMouse(0, HeaderPoint(id), delta);
            return NodeScreenRect(id).Min.x - before;
        }

        float PanBy(ImVec2 delta)
        {
            const float before = CanvasOrigin().x;
            DragMouse(1, EmptySpot(), delta);
            return CanvasOrigin().x - before;
        }

        void Key(ImGuiKey key)
        {
            ImGui::GetIO().AddKeyEvent(key, true);
            Run(2);
            ImGui::GetIO().AddKeyEvent(key, false);
            Run(20);
        }

        bool PopupOpen() const { return ImGui::GetCurrentContext()->OpenPopupStack.Size > 0; }
        std::vector<int> NodeIds() const
        {
            std::vector<int> ids;
            for (const auto& node : Layer->CurrentGraph().Nodes) ids.push_back(node.Id);
            return ids;
        }
    };
}

TEST_CASE("Graph canvas nodes can be dragged and the view panned", "[ui][graph][canvas]")
{
    CanvasRig rig;
    rig.Settle();

    SECTION("left-drag moves every node")
    {
        for (const int id : rig.NodeIds())
        {
            const float moved = rig.DragNodeBy(id, kDragDelta);
            INFO("node " << id << " moved " << moved << " " << rig.Probe());
            CHECK(moved > 30.0f);
        }
    }

    SECTION("right-drag pans the view")
    {
        const float moved = rig.PanBy(kPanDelta);
        INFO("panned " << moved << " " << rig.Probe());
        CHECK(moved < -60.0f);
    }

    SECTION("pan and drag work right after focusing another window")
    {
        const ImRect panel = rig.PanelRect();
        rig.Click(0, ImVec2(panel.Min.x - 100.0f, panel.GetCenter().y));
        const float panned = rig.PanBy(kPanDelta);
        INFO(rig.Probe());
        CHECK(panned < -60.0f);
        rig.Click(0, ImVec2(panel.Min.x - 100.0f, panel.GetCenter().y));
        CHECK(rig.DragNodeBy(rig.NodeIds().front(), kDragDelta) > 30.0f);
    }

    SECTION("pan and drag work after a node tooltip was shown")
    {
        const int id = rig.NodeIds().front();
        const ImVec2 over = rig.HeaderPoint(id);
        rig.MoveTo(over, 120);
        CHECK(rig.Layer->m_HoverNodeTime > 0.6f);
        const float before = rig.CanvasOrigin().x;
        rig.Press(1);
        for (int i = 1; i <= 10; ++i) rig.MoveTo(ImVec2(over.x + kPanDelta.x * i / 10.0f, over.y + kPanDelta.y * i / 10.0f), 1);
        rig.Release(1);
        CHECK(rig.CanvasOrigin().x - before < -60.0f);
        rig.MoveTo(rig.HeaderPoint(id), 120);
        CHECK(rig.DragNodeBy(id, kDragDelta) > 30.0f);
    }

    SECTION("a physical mouse delivers bursts of motion events per frame")
    {
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 from = rig.EmptySpot();
        const float before = rig.CanvasOrigin().x;
        for (int i = 0; i < 20; ++i) io.AddMousePosEvent(from.x - i * 0.2f, from.y);
        io.AddMouseButtonEvent(1, true);
        for (int step = 1; step <= 60; ++step)
        {
            for (int sub = 0; sub < 8; ++sub) io.AddMousePosEvent(from.x - step * 2.0f - sub * 0.25f, from.y - step * 0.5f);
            rig.Run(1);
        }
        io.AddMouseButtonEvent(1, false);
        rig.Run(5);
        CHECK(rig.CanvasOrigin().x - before < -60.0f);
    }
}

TEST_CASE("Graph canvas still pans and drags after the palette spawned a node", "[ui][graph][canvas]")
{
    CanvasRig rig;
    rig.Settle();

    SECTION("opened from a background right-click")
    {
        rig.Click(1, rig.EmptySpot());
        REQUIRE(rig.PopupOpen());
        const std::size_t nodes = rig.Layer->CurrentGraph().Nodes.size();
        rig.Key(ImGuiKey_Enter);
        CHECK(rig.Layer->CurrentGraph().Nodes.size() == nodes + 1);
        CHECK_FALSE(rig.PopupOpen());
    }

    SECTION("opened by dropping a link on empty space")
    {
        const Node& first = rig.Layer->CurrentGraph().Nodes.front();
        const int pinId = first.Outputs.front().Id;
        const ImRect rect = rig.NodeScreenRect(first.Id);
        ImVec2 pin(0.0f, 0.0f);
        for (float y = rect.Min.y; y < rect.Max.y && pin.x == 0.0f; y += 3.0f)
            for (float x = rect.Max.x - 40.0f; x < rect.Max.x && pin.x == 0.0f; x += 3.0f)
            {
                rig.MoveTo(ImVec2(x, y), 1);
                if (rig.Layer->m_HoveredPinId == pinId) pin = ImVec2(x, y);
            }
        REQUIRE(pin.x != 0.0f);
        const ImVec2 empty = rig.EmptySpot();
        rig.DragMouse(0, pin, ImVec2(empty.x - pin.x, empty.y - pin.y));
        rig.Run(5);
        REQUIRE(rig.PopupOpen());
        const std::size_t nodes = rig.Layer->CurrentGraph().Nodes.size();
        rig.Key(ImGuiKey_Enter);
        CHECK(rig.Layer->CurrentGraph().Nodes.size() == nodes + 1);
    }

    const float panned = rig.PanBy(kPanDelta);
    INFO("panned " << panned << " " << rig.Probe());
    CHECK(panned < -60.0f);
    const float moved = rig.DragNodeBy(rig.NodeIds().front(), kDragDelta);
    INFO("moved " << moved << " " << rig.Probe());
    CHECK(moved > 30.0f);
}

TEST_CASE("Graph canvas pans from anywhere in the panel", "[ui][graph][canvas]")
{
    CanvasRig rig;
    rig.Settle();
    const ImRect panel = rig.PanelRect();
    std::string failures;
    for (float y = panel.Min.y + 25.0f; y < panel.Max.y - 25.0f; y += panel.GetHeight() / 9.0f)
        for (float x = panel.Min.x + 25.0f; x < panel.Max.x - 25.0f; x += panel.GetWidth() / 12.0f)
        {
            const float before = rig.CanvasOrigin().x;
            rig.DragMouse(1, ImVec2(x, y), ImVec2(-30.0f, -10.0f));
            if (rig.CanvasOrigin().x - before > -15.0f) failures += std::to_string(static_cast<int>(x)) + "," + std::to_string(static_cast<int>(y)) + " ";
            rig.MoveTo(ImVec2(x - 30.0f, y - 10.0f), 1);
            rig.Run(10);
        }
    INFO("pan failed from: " << failures);
    CHECK(failures.empty());
}

TEST_CASE("Every palette node type can be dragged and its canvas panned", "[ui][graph][canvas]")
{
    const auto registry = BuildNodeRegistry();
    std::string failures;
    for (const auto& entry : registry.Entries())
    {
        CanvasRig rig([&](AnimationGraphData& graph)
        {
            const auto ids = entry.Spawn(graph);
            for (const int id : ids)
                if (auto* node = graph.FindNode(id)) node->Position = {60.0f, 60.0f};
            if (graph.Nodes.empty()) return;
            Node anchor = CreateStartEventNode(graph.AllocateId());
            anchor.Position = {600.0f, 330.0f};
            graph.Nodes.push_back(anchor);
        });
        if (rig.Layer->CurrentGraph().Nodes.empty()) continue;
        rig.Settle();
        if (rig.DragNodeBy(rig.NodeIds().front(), kDragDelta) < 30.0f) failures += entry.Name + " drag; ";
        if (rig.PanBy(kPanDelta) > -60.0f) failures += entry.Name + " pan; ";
    }
    INFO("failures: " << failures);
    CHECK(failures.empty());
}
