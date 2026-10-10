#include <catch2/catch_test_macros.hpp>
#include <imgui.h>

import GPP;
import node_editor;
import std;

namespace ed = ax::NodeEditor;

namespace
{
    struct Harness
    {
        ImGuiContext* Ctx = nullptr;
        ed::EditorContext* Editor = nullptr;
        std::vector<int> Nodes{1, 2};
        std::vector<int> Deleted;
        bool Focused = false;
        bool Hovered = false;
        int Selected = 0;
        ImVec2 NodePos, NodeSize, NodeScreen; int HotNode = 0;

        Harness()
        {
            Ctx = ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2(800, 600);
            io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
            unsigned char* pixels = nullptr;
            int w = 0, h = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
            ed::Config config;
            config.SettingsFile = nullptr;
            Editor = ed::CreateEditor(&config);
        }

        ~Harness()
        {
            ed::DestroyEditor(Editor);
            ImGui::DestroyContext(Ctx);
        }

        void Frame()
        {
            ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(700, 500));
            ImGui::Begin("Animation Graph");
            ImGui::BeginGroup();
            ImGui::BeginChild("AnimationGraphEditorPanel", ImVec2(500, 400), true);
            ed::SetCurrentEditor(Editor);
            Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
            ed::Begin("AnimationGraphCanvas");
            for (const int id : Nodes)
            {
                if (!m_Placed) ed::SetNodePosition(ed::NodeId(id), ImVec2(50.0f + 200.0f * (id - 1), 50.0f));
                ed::BeginNode(ed::NodeId(id));
                ImGui::Text("Node %d", id);
                ed::BeginPin(ed::PinId(id * 10), ed::PinKind::Input);
                ImGui::Text("in");
                ed::EndPin();
                ed::EndNode();
            }
            if (!m_Placed) ed::NavigateToContent(0.0f);
            m_Placed = true;
            Selected = ed::GetSelectedObjectCount();
            NodePos = ed::GetNodePosition(ed::NodeId(1)); NodeSize = ed::GetNodeSize(ed::NodeId(1)); NodeScreen = ed::CanvasToScreen(ImVec2(0,0)); HotNode = (int)ed::GetHoveredNode().Get();
            Hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
            if (ed::BeginDelete())
            {
                ed::NodeId id;
                while (ed::QueryDeletedNode(&id))
                {
                    if (ed::AcceptDeletedItem()) Deleted.push_back(static_cast<int>(id.Get()));
                }
            }
            ed::EndDelete();
            ed::End();
            ed::SetCurrentEditor(nullptr);
            ImGui::EndChild();
            ImGui::EndGroup();
            ImGui::End();
            ImGui::Render();
            for (const int id : Deleted) std::erase(Nodes, id);
        }

        void Run(int frames)
        {
            for (int i = 0; i < frames; ++i) Frame();
        }

        bool m_Placed = false;
    };
}

TEST_CASE("Delete key removes the selected graph node", "[ui][graph][delete]")
{
    Harness h;
    h.Run(5);
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 target(0, 0);
    for (float y = 20; y < 400 && h.HotNode == 0; y += 10)
        for (float x = 20; x < 500 && h.HotNode == 0; x += 10)
        {
            target = ImVec2(x, y);
            io.AddMousePosEvent(x, y);
            h.Run(1);
        }
    INFO("target=" << target.x << "," << target.y);
    io.AddMouseButtonEvent(0, true);
    h.Run(3);
    io.AddMouseButtonEvent(0, false);
    h.Run(3);
    INFO("focused=" << h.Focused << " hovered=" << h.Hovered << " selected=" << h.Selected << " pos=" << h.NodeScreen.x << "," << h.NodeScreen.y << " size=" << h.NodeSize.x << "," << h.NodeSize.y << " hot=" << h.HotNode);
    io.AddKeyEvent(ImGuiKey_Delete, true);
    h.Run(2);
    io.AddKeyEvent(ImGuiKey_Delete, false);
    h.Run(3);
    CHECK(h.Deleted.size() == 1);
}

TEST_CASE("The ImGui backend maps the Delete key", "[ui][graph][delete]")
{
    CHECK(GPP::ToImGuiKey(GPP::KeyCode::Delete) == ImGuiKey_Delete);
}
