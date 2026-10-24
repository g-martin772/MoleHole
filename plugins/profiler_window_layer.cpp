#include <imgui.h>

import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    constexpr float kLaneHeight = 13.0f;
    constexpr std::size_t kMaxLanes = 7;

    ImU32 ZoneColor(const char* name, const float alpha = 1.0f)
    {
        const auto hash = std::hash<std::string_view>{}(name);
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(static_cast<float>(hash % 360) / 360.0f, 0.55f, 0.85f, r, g, b);
        return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
    }

    struct ProfilerWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, Renderer, UiState>;

        ProfilerWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<Renderer> renderer,
                            std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_Renderer(std::move(renderer)), m_UiState(std::move(uiState))
        {
        }

        void OnDetach() override { Profiler::SetEnabled(false); }

        void OnUiRender() override
        {
            const bool wanted = m_UiState->ShowProfilerWindow;
            if (!wanted)
            {
                if (m_Enabled)
                {
                    Profiler::SetEnabled(false);
                    m_Enabled = false;
                }
                return;
            }
            if (!m_Enabled)
            {
                Profiler::SetEnabled(true);
                m_Enabled = true;
            }
            ImGui::SetNextWindowSize(ImVec2(820, 700), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Profiler", &m_UiState->ShowProfilerWindow))
            {
                ImGui::End();
                return;
            }
            if (!m_Frozen) m_Collector.Update();

            ImGui::Checkbox("Freeze", &m_Frozen);
            ImGui::SameLine();
            ImGui::TextDisabled("CPU zones are recorded only while this window is open; timeline spans the last %.1f s",
                                static_cast<double>(m_Collector.WindowNs()) * 1e-9);

            DrawTables();
            DrawTimeline();
            ImGui::End();
        }

    private:
        void DrawTables()
        {
            SectionHeader("CPU ZONES  |  GPU PASSES");
            const float height = std::max(160.0f, ImGui::GetContentRegionAvail().y * 0.6f);
            if (!ImGui::BeginTable("##profiler-split", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
                return;
            ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthStretch, 0.68f);
            ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthStretch, 0.32f);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::BeginChild("##cpu", ImVec2(0, height));
            DrawCpuTables();
            ImGui::EndChild();
            ImGui::TableNextColumn();
            ImGui::BeginChild("##gpu", ImVec2(0, height));
            DrawGpuPasses();
            ImGui::EndChild();
            ImGui::EndTable();
        }

        void DrawCpuTables()
        {
            const auto& threads = m_Collector.Threads();
            if (threads.empty())
            {
                ImGui::TextDisabled("Waiting for zones...");
                return;
            }
            for (const auto& thread : threads)
            {
                if (thread.Zones.empty()) continue;
                ImGui::PushID(static_cast<int>(thread.Id));
                const bool open = ImGui::CollapsingHeader(thread.Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                if (open && ImGui::BeginTable("zones", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
                {
                    ImGui::TableSetupColumn("Zone", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("calls");
                    ImGui::TableSetupColumn("last ms");
                    ImGui::TableSetupColumn("avg ms");
                    ImGui::TableSetupColumn("max ms");
                    ImGui::TableHeadersRow();
                    for (const auto& zone : thread.Zones)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Indent(static_cast<float>(zone.Depth) * 8.0f);
                        ImGui::TextUnformatted(zone.Name.c_str());
                        ImGui::Unindent(static_cast<float>(zone.Depth) * 8.0f);
                        ImGui::TableNextColumn();
                        ImGui::Text("%u", zone.Calls);
                        ImGui::TableNextColumn();
                        ImGui::Text("%.3f", zone.LastMs);
                        ImGui::TableNextColumn();
                        ImGui::Text("%.3f", zone.AvgMs);
                        ImGui::TableNextColumn();
                        ImGui::TextColored(zone.MaxMs > 16.0 ? ImVec4(0.95f, 0.6f, 0.3f, 1.0f) : ImVec4(1, 1, 1, 1), "%.3f",
                                           zone.MaxMs);
                    }
                    ImGui::EndTable();
                }
                ImGui::PopID();
            }
        }

        void DrawGpuPasses()
        {
            const auto stats = m_Renderer->GetBufferTargetStats(kViewportBufferId);
            if (!stats || stats->Passes.empty())
            {
                ImGui::TextDisabled("No GPU pass timings yet.");
                return;
            }
            ImGui::Text("Viewport GPU %.2f ms in %u submissions", stats->GpuMs, stats->Submissions);
            if (!ImGui::BeginTable("gpu", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) return;
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("ms");
            ImGui::TableSetupColumn("share", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();
            double total = 0.0;
            for (const auto& pass : stats->Passes) total += pass.Ms;
            for (const auto& pass : stats->Passes)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(pass.Name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", pass.Ms);
                ImGui::TableNextColumn();
                ImGui::ProgressBar(total > 0.0 ? static_cast<float>(pass.Ms / total) : 0.0f, ImVec2(-1, 10), "");
            }
            ImGui::EndTable();
        }

        void DrawTimeline()
        {
            SectionHeader("TIMELINE");
            const double windowNs = static_cast<double>(m_Collector.WindowNs());
            const double nowNs = static_cast<double>(m_Collector.NowNs());
            const float width = std::max(100.0f, ImGui::GetContentRegionAvail().x);
            auto* list = ImGui::GetWindowDrawList();
            for (const auto& thread : m_Collector.Threads())
            {
                if (thread.Timeline.empty()) continue;
                std::size_t lanes = 1;
                for (const auto& record : thread.Timeline) lanes = std::max<std::size_t>(lanes, record.Depth + 1u);
                lanes = std::min(lanes, kMaxLanes);
                ImGui::TextDisabled("%s", thread.Name.c_str());
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                const float height = static_cast<float>(lanes) * kLaneHeight;
                ImGui::InvisibleButton(std::format("##timeline-{}", thread.Id).c_str(), ImVec2(width, height));
                list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                                    ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.35f)));
                const ProfileRecord* hovered = nullptr;
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                for (const auto& record : thread.Timeline)
                {
                    if (record.Depth >= kMaxLanes) continue;
                    const double startMs = (static_cast<double>(record.StartNs) - (nowNs - windowNs)) / windowNs;
                    const float x0 = origin.x + static_cast<float>(std::clamp(startMs, 0.0, 1.0)) * width;
                    const float x1 = std::max(x0 + 1.0f, origin.x + static_cast<float>(std::clamp(
                                         startMs + static_cast<double>(record.DurationNs) / windowNs, 0.0, 1.0)) * width);
                    const float y0 = origin.y + static_cast<float>(record.Depth) * kLaneHeight;
                    list->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + kLaneHeight - 1.0f), ZoneColor(record.Name, 0.9f));
                    if (ImGui::IsItemHovered() && mouse.x >= x0 && mouse.x <= x1 && mouse.y >= y0 &&
                        mouse.y < y0 + kLaneHeight)
                    {
                        hovered = &record;
                    }
                }
                if (hovered)
                {
                    ImGui::SetTooltip("%s  %.3f ms", hovered->Name, static_cast<double>(hovered->DurationNs) * 1e-6);
                }
            }
        }

        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<UiState> m_UiState;
        ProfileCollector m_Collector;
        bool m_Enabled = false;
        bool m_Frozen = false;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(ProfilerWindowLayer)
