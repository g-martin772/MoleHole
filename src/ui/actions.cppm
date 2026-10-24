export module MoleHole:UI.Actions;

import std;
import GPP;
import :Rendering.Settings;
import :Rendering.SettingsService;
import :UI.State;
import :UI.ActionLogic;
import :UI.CreateCatalog;
import :UI.CreateMenu;
import :UI.EditLogic;
import :UI.TransportLogic;
import :UI.TutorialState;

export namespace MoleHole
{
    struct TemplateInfo
    {
        std::string Label;
        std::string Path;
    };

    struct ActionSources
    {
        std::vector<TemplateInfo> Templates;
        std::vector<CreateEntry> CreateEntries;
        std::vector<std::string> ProfileNames;
        std::vector<std::string> LayoutNames;
    };

    struct ActionHost
    {
        UiState* Ui = nullptr;
        std::shared_ptr<GPP::SimulationRunner> Runner;
        RenderSettingsService* Render = nullptr;
    };

    [[nodiscard]] inline std::vector<TemplateInfo> CollectTemplates(const std::filesystem::path& directory)
    {
        std::vector<TemplateInfo> result;
        std::error_code ec;
        if (!std::filesystem::exists(directory, ec)) return result;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
        {
            if (ec) break;
            if (entry.path().extension() != ".yaml") continue;
            result.push_back({entry.path().stem().string(), entry.path().string()});
        }
        std::ranges::sort(result, {}, &TemplateInfo::Label);
        return result;
    }

    [[nodiscard]] inline ActionContext MakeActionContext(const UiState& ui, const GPP::SimulationRunner* runner)
    {
        ActionContext context;
        context.HasScene = runner != nullptr;
        context.HasSelection = runner && ui.SelectedEntityGuid != 0;
        context.Paused = !runner || runner->IsPaused();
        context.CanUndo = runner && runner->CanUndo();
        context.CanRedo = runner && runner->CanRedo();
        return context;
    }

    namespace ActionDetail
    {
        class Builder
        {
        public:
            explicit Builder(const ActionHost& host) : m_Host(host) {}

            ActionDescriptor& Add(std::string id, std::string label, std::string category, std::function<void()> invoke = {})
            {
                auto& action = m_Actions.emplace_back();
                action.Id = std::move(id);
                action.Label = std::move(label);
                action.Category = std::move(category);
                action.Invoke = std::move(invoke);
                return action;
            }

            ActionDescriptor& Key(std::string id, std::string label, std::string category, std::string shortcut, std::string scope,
                                  std::function<void()> invoke = {})
            {
                auto& action = Add(std::move(id), std::move(label), std::move(category), std::move(invoke));
                action.Shortcut = std::move(shortcut);
                action.Scope = std::move(scope);
                return action;
            }

            ActionDescriptor& Defer(std::string id, std::string label, std::string category, std::string shortcut = {},
                                    std::string arg = {})
            {
                auto invoke = [ui = m_Host.Ui, key = id, arg = std::move(arg)]
                {
                    if (ui) ui->Actions.Push({key, arg});
                };
                return Key(std::move(id), std::move(label), std::move(category), std::move(shortcut), {}, std::move(invoke));
            }

            ActionDescriptor& Edit(std::string id, std::string label, std::string shortcut, const EditAction what)
            {
                auto& action = Key(std::move(id), std::move(label), "Edit", std::move(shortcut), "Scene / Viewport",
                                   [ui = m_Host.Ui, what] { if (ui) ui->Edits.Push(what); });
                action.Needs = ActionNeed::Scene | ActionNeed::Selection;
                return action;
            }

            ActionDescriptor& Window(std::string id, std::string label, bool* flag)
            {
                auto& action = Add(std::move(id), std::move(label), "View", [flag] { if (flag) *flag = !*flag; });
                action.Toggle = flag;
                action.Keywords = "window panel show hide toggle";
                return action;
            }

            ActionDescriptor& Gizmo(std::string id, std::string label, std::string shortcut, const GizmoOperation operation)
            {
                return Key(std::move(id), std::move(label), "Gizmo", std::move(shortcut), "Viewport",
                           [ui = m_Host.Ui, operation] { if (ui) ui->ActiveGizmoOperation = operation; });
            }

            ActionDescriptor& Transport(std::string id, std::string label, std::string shortcut, std::function<void()> invoke,
                                        const ActionNeed needs = ActionNeed::Scene)
            {
                auto& action = Key(std::move(id), std::move(label), "Simulation", std::move(shortcut), {}, std::move(invoke));
                action.Needs = needs;
                return action;
            }

            std::vector<ActionDescriptor> Take() { return std::move(m_Actions); }

            [[nodiscard]] const ActionHost& Host() const noexcept { return m_Host; }

        private:
            ActionHost m_Host;
            std::vector<ActionDescriptor> m_Actions;
        };

        inline void SetSpeed(const ActionHost& host, const int direction)
        {
            if (!host.Runner || !host.Ui) return;
            const double scale = NextSpeedPreset(host.Runner->Metadata().TimeScale, direction);
            if (host.Ui->PlaySessionActive)
                host.Runner->EnqueueSystemEdit([scale](GPP::Scene& scene) { scene.SetTimeScale(scale); }, false);
            else host.Runner->EnqueueEdit([scale](GPP::Scene& scene) { scene.SetTimeScale(scale); });
        }

        inline void AddFile(Builder& b, const ActionSources& sources)
        {
            b.Defer("file.new", "New Scene", "File", "Ctrl+N");
            b.Defer("file.open", "Open Scene...", "File", "Ctrl+O");
            b.Defer("file.save", "Save Scene", "File", "Ctrl+S").Needs = ActionNeed::Scene;
            b.Defer("file.save_as", "Save Scene As...", "File", "Ctrl+Shift+S").Needs = ActionNeed::Scene;
            b.Defer("file.close", "Close Scene (Launcher)", "File");
            b.Defer("file.launcher", "Open Launcher in New Window", "File");
            b.Defer("file.export", "Export Render...", "File").Needs = ActionNeed::Scene;
            b.Defer("file.quit", "Quit", "File");
            for (const auto& entry : sources.Templates)
            {
                auto& action = b.Defer("file.template", "New from Template: " + entry.Label, "File", {}, entry.Path);
                action.Id += ":" + entry.Label;
                action.Keywords = "scene example";
            }
            auto* ui = b.Host().Ui;
            auto& autosave = b.Add("file.autosave", "Auto Save", "File", [ui] { if (ui) ui->AutosaveEnabled = !ui->AutosaveEnabled; });
            autosave.Toggle = ui ? &ui->AutosaveEnabled : nullptr;
        }

        inline void AddEdit(Builder& b)
        {
            auto* ui = b.Host().Ui;
            const auto history = ActionNeed::Scene | ActionNeed::Paused;
            b.Defer("edit.undo", "Undo", "Edit", "Ctrl+Z").Needs = history | ActionNeed::CanUndo;
            b.Defer("edit.redo", "Redo", "Edit", "Ctrl+Shift+Z / Ctrl+Y").Needs = history | ActionNeed::CanRedo;
            b.Edit("edit.select_all", "Select All", "Ctrl+A", EditAction::SelectAll).Needs = ActionNeed::Scene;
            b.Edit("edit.duplicate", "Duplicate", "Ctrl+D", EditAction::Duplicate);
            b.Edit("edit.delete", "Delete", "Del", EditAction::Delete);
            b.Edit("edit.rename", "Rename", "F2", EditAction::Rename);
            b.Edit("edit.frame", "Frame Selected", "F", EditAction::FrameSelected);
            b.Edit("edit.deselect", "Deselect", "Esc", EditAction::Deselect);
            b.Key("edit.create", "Add Entity Popup", "Edit", "Shift+A", "Scene / Viewport",
                  [ui] { if (ui) ui->CreatePopup.RequestAtMouse(); }).Needs = ActionNeed::Scene;
            auto& historyWindow = b.Add("edit.history", "Undo History", "Edit", [ui] { if (ui) ui->ShowUndoHistoryWindow = !ui->ShowUndoHistoryWindow; });
            historyWindow.Toggle = ui ? &ui->ShowUndoHistoryWindow : nullptr;
            b.Add("edit.preferences", "Preferences...", "Edit", [ui] { if (ui) ui->ShowSettingsWindow = true; });
        }

        inline void AddView(Builder& b, const ActionSources& sources)
        {
            auto* ui = b.Host().Ui;
            static bool none = false;
            const auto flag = [ui](bool UiState::*member) { return ui ? &(ui->*member) : &none; };
            b.Window("view.camera", "Camera", flag(&UiState::ShowCameraWindow));
            b.Window("view.system", "System", flag(&UiState::ShowSystemWindow));
            b.Window("view.profiler", "Profiler", flag(&UiState::ShowProfilerWindow));
            b.Window("view.log", "Log", flag(&UiState::ShowLogWindow));
            b.Window("view.status_bar", "Status Bar", flag(&UiState::ShowStatusBar));
            b.Window("view.scene", "Scene", flag(&UiState::ShowSceneWindow));
            b.Window("view.render", "Render", flag(&UiState::ShowRenderWindow));
            b.Window("view.simulation", "Simulation", flag(&UiState::ShowSimulationWindow));
            b.Window("view.hud", "Viewport HUD", flag(&UiState::ShowViewportHud));
            b.Window("view.camera_gizmos", "Camera Gizmos", flag(&UiState::ShowCameraGizmos));
            b.Window("view.gr", "General Relativity", flag(&UiState::ShowGeneralRelativityWindow));
            b.Window("view.science", "Science", flag(&UiState::ShowScienceWindow));
            b.Window("view.graph", "Animation Graph", flag(&UiState::ShowAnimationGraphWindow));
            b.Defer("layout.reset", "Reset Layout", "View").Keywords = "layout dock windows default arrange";
            b.Add("layout.save", "Save Layout As...", "View", [ui] { if (ui) ui->LayoutSaveRequested = true; }).Keywords =
                "layout dock windows preset store";
            for (const auto& name : sources.LayoutNames)
            {
                auto& action = b.Defer("layout.apply", "Layout: " + name, "View", {}, name);
                action.Id += ":" + name;
                action.Keywords = "layout dock windows preset apply";
            }
        }

        inline void AddGizmo(Builder& b)
        {
            auto* ui = b.Host().Ui;
            b.Gizmo("gizmo.translate", "Gizmo: Translate", "T / 1", GizmoOperation::Translate);
            b.Gizmo("gizmo.rotate", "Gizmo: Rotate", "R / 2", GizmoOperation::Rotate);
            b.Gizmo("gizmo.scale", "Gizmo: Scale", "S / 3", GizmoOperation::Scale);
            b.Add("gizmo.world", "Gizmo: World Space", "Gizmo", [ui] { if (ui) ui->ActiveGizmoMode = GizmoMode::World; });
            b.Add("gizmo.local", "Gizmo: Local Space", "Gizmo", [ui] { if (ui) ui->ActiveGizmoMode = GizmoMode::Local; });
            auto& snap = b.Add("gizmo.snap", "Gizmo: Snapping", "Gizmo", [ui] { if (ui) ui->GizmoSnapEnabled = !ui->GizmoSnapEnabled; });
            snap.Toggle = ui ? &ui->GizmoSnapEnabled : nullptr;
        }

        inline void AddSimulation(Builder& b)
        {
            const auto host = b.Host();
            b.Transport("sim.play", "Play / Pause", {}, [host]
            {
                if (!host.Runner || !host.Ui) return;
                if (host.Runner->IsPaused()) host.Ui->PendingSnapshotForPlay = true;
                else host.Runner->SetPaused(true);
            });
            b.Transport("sim.stop", "Stop (restore the scene)", {}, [ui = host.Ui] { if (ui) ui->PendingStopSimulation = true; });
            b.Transport("sim.step", "Step One Tick", ".", [ui = host.Ui]
            {
                if (ui) ui->Transport.Push({TransportRequest::Kind::Step, StepTicks(false, ui->TransportStepCount)});
            }, ActionNeed::Scene | ActionNeed::Paused);
            b.Transport("sim.step_n", "Step Several Ticks", "Shift+.", [ui = host.Ui]
            {
                if (ui) ui->Transport.Push({TransportRequest::Kind::Step, StepTicks(true, ui->TransportStepCount)});
            }, ActionNeed::Scene | ActionNeed::Paused);
            b.Transport("sim.slower", "Slower (previous speed preset)", "[ / Num -", [host] { SetSpeed(host, -1); });
            b.Transport("sim.faster", "Faster (next speed preset)", "] / Num +", [host] { SetSpeed(host, 1); });
            b.Transport("sim.snapshot", "Take Snapshot", {}, [ui = host.Ui]
            {
                if (ui) ui->Transport.Push({TransportRequest::Kind::Snapshot});
            });
        }

        inline void AddRender(Builder& b, const ActionSources& sources)
        {
            const auto host = b.Host();
            auto& reference = b.Add("render.reference", "Reference View", "Render", [ui = host.Ui] { if (ui) ui->ReferenceView = !ui->ReferenceView; });
            reference.Toggle = host.Ui ? &host.Ui->ReferenceView : nullptr;
            auto& stats = b.Add("render.stats", "Stats Overlay", "Render", [ui = host.Ui] { if (ui) ui->ShowRenderStats = !ui->ShowRenderStats; });
            stats.Toggle = host.Ui ? &host.Ui->ShowRenderStats : nullptr;
            for (const auto& name : sources.ProfileNames)
            {
                auto& action = b.Add("render.profile:" + name, "Viewport Profile: " + name, "Render", [render = host.Render, name]
                {
                    if (!render) return;
                    render->Assign(RenderContext::Edit, name);
                    render->Assign(RenderContext::Play, name);
                });
                action.Keywords = "render quality";
            }
        }

        inline void AddCreate(Builder& b, const ActionSources& sources)
        {
            const auto host = b.Host();
            for (const auto& entry : sources.CreateEntries)
            {
                std::string label = "Create: " + (entry.Folder.empty() ? entry.Name : entry.Folder + " / " + entry.Name);
                auto& action = b.Add("create:" + entry.Id, std::move(label), "Create", [host, entry]
                {
                    if (host.Runner && host.Ui) CreateFromEntry(*host.Runner, *host.Ui, entry);
                });
                action.Needs = ActionNeed::Scene;
                action.Keywords = entry.Keywords;
            }
        }

        inline void AddHelp(Builder& b)
        {
            auto* ui = b.Host().Ui;
            b.Key("help.palette", "Command Palette...", "Help", "Ctrl+P / Ctrl+Shift+P", {}, [ui] { if (ui) ui->PaletteRequested = true; });
            b.Key("help.shortcuts", "Keyboard Shortcuts", "Help", "F1", {}, [ui] { if (ui) ui->ShowShortcutSheet = !ui->ShowShortcutSheet; });
            b.Add("help.tutorial", "Start Tutorial", "Help", [ui] { if (ui) StartTutorial(*ui); });
            b.Add("help.about", "About", "Help", [ui] { if (ui) ui->ShowSettingsWindow = true; });
        }

        inline void AddGraphEditor(Builder& b)
        {
            constexpr const char* kScope = "Graph Editor";
            b.Key("graph.undo", "Undo", "Graph Editor", "Ctrl+Z", kScope);
            b.Key("graph.redo", "Redo", "Graph Editor", "Ctrl+Shift+Z / Ctrl+Y", kScope);
            b.Key("graph.select_all", "Select All", "Graph Editor", "Ctrl+A", kScope);
            b.Key("graph.copy", "Copy", "Graph Editor", "Ctrl+C", kScope);
            b.Key("graph.cut", "Cut", "Graph Editor", "Ctrl+X", kScope);
            b.Key("graph.paste", "Paste", "Graph Editor", "Ctrl+V", kScope);
            b.Key("graph.duplicate", "Duplicate", "Graph Editor", "Ctrl+D", kScope);
            b.Key("graph.delete", "Delete Selection", "Graph Editor", "Backspace / Del", kScope);
            b.Key("graph.comment", "Comment Selection", "Graph Editor", "C", kScope);
            b.Key("graph.breakpoint", "Toggle Breakpoint", "Graph Editor", "F9", kScope);
        }
    }

    [[nodiscard]] inline std::vector<ActionDescriptor> BuildActions(const ActionHost& host, const ActionSources& sources = {})
    {
        ActionDetail::Builder builder(host);
        ActionDetail::AddFile(builder, sources);
        ActionDetail::AddEdit(builder);
        ActionDetail::AddView(builder, sources);
        ActionDetail::AddGizmo(builder);
        ActionDetail::AddSimulation(builder);
        ActionDetail::AddRender(builder, sources);
        ActionDetail::AddCreate(builder, sources);
        ActionDetail::AddHelp(builder);
        ActionDetail::AddGraphEditor(builder);
        return builder.Take();
    }

    [[nodiscard]] inline const std::vector<ActionDescriptor>& StaticActions()
    {
        static const auto actions = BuildActions({});
        return actions;
    }

    [[nodiscard]] inline std::string ActionShortcut(const std::string_view id)
    {
        const auto* action = FindAction(StaticActions(), id);
        return action ? PrimaryShortcut(action->Shortcut) : std::string();
    }

    [[nodiscard]] inline std::string ActionDisabledReason(const std::string_view id, const ActionContext& context)
    {
        const auto* action = FindAction(StaticActions(), id);
        return action ? DisabledReason(action->Needs, context) : std::string();
    }
}
