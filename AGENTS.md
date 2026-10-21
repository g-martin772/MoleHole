# AGENTS.md

Notes for AI agents. Read first.

## Repos
- MoleHole = client app (this repo, branch `gpp-framework`)
- gpp = framework, separate git repo `../gpp`, symlinked at `dependencies/gpp`
- Backend / reusable code → gpp. App-specific (UI, black hole, graph editor) → MoleHole
- Commits go in the repo that owns the change. Check `git -C dependencies/gpp status` too

## Rules (user preferences)
- Bare-minimum comments
- Scenes = ONE yaml file. No project folders
- One commit per major feature. Small unrelated fixes: `Fix: ...`. Short messages
- NO `Co-Authored-By` trailers
- Never push. Never commit unless asked
- Commit with explicit paths only (user keeps staged files, e.g. `docs/migration-todo.md`)
- Don't change layout/visuals blindly. Reproduce first, change one thing
- Adding component/field = one-place change (descriptor). Same for render settings: `RenderSettings` field + `BuildRegistry()` entry
- C++23, `import std`, modules (`.cppm`). No headers for own code
- Match surrounding style

## Build / test
- Env: `VCPKG_ROOT` set, CMake ≥ 4.4, Ninja, GCC (import std)
- `cmake --preset vcpkg && cmake --build --preset vcpkg`
- Binary: `build/MoleHole` (run from repo root, reads `config.json`, `scripts/`, `combos/`, `shaders/`)
- Tests: `build/molehole_tests` (Catch2, `tests/unit/**`); gpp has own tests in `../gpp/tests`
- Plugins (`plugins/*.cpp`) are hot-reload `.so`s. Rebuild → app reloads. Stale `.so` = stale behavior
- `molehole_core` (incl. `RenderPipeline`) is not hot-reloaded: restart the app after changing it
- Known failing test: template-scene test if `templates/test-scene.yaml` was edited

## Layout
- `src/main.cpp` – CLI args, DI services, layer registration
- `src/molehole.cppm` – module root, partition exports
- `src/simulation/` – components, animation graph (IR, transpiler, validation, registry, combos, history)
- `src/rendering/` – `RenderSettings` + profiles (`render_settings.*`, `render_settings_service.cppm`), `RenderPipeline` stages, sidecar, `RenderFrame`
- `src/application/` – app state, `ExportController`, `SceneLoader`
- `src/ui/` – `UiState`, widget kit, theme helpers
- `plugins/` – one UI layer per file (+ `theme.cpp`)
- `scripts/*.luau` – reusable script components
- `combos/*.yaml` – graph combo templates
- `templates/*.yaml` – example scenes
- gpp: `core/` (DI, assets, hotreload, threading), `graphics/` (vulkan, ui backend, shaders), `simulation/` (scene, ECS, runner, commands, physx), `scripting/` (Luau)

## Architecture
- Sim thread owns private `Scene`; UI reads published snapshots (dirty-tracked)
- UI → sim: `runner.EnqueueCommand(...)` (undoable, serializable) or `EnqueueTrackedEdit`
- `CommandOptions`: `Undoable`, `OnlyWhilePlaying`, label/key for merge
- Modules: `ISimulationModule` with phases; `GetModule<T>()`
- Components: `RegisterComponent<T>(name, ComponentDescription<T>{Fields})` → YAML, inspector, graph pins, add-menu all generated
- Graph: editor → IR → Luau transpile (cached). Interpreter = fallback. Runtime picked by dropdown (`m_UseLuau`)
- Scripts: Luau, sandboxed, coroutine scheduler for latent nodes
- Graphs stored in scene YAML `Graphs:` (schema versioned)
- Asset dirs via `AssetDirectories` service, configured in `config.json` `GPP.Assets`

## Verify and debug (mandatory)
Never report a UI/render change as done on "it builds" alone.
1. **Always do a short image export** and look at it: `build/MoleHole --export-image $SCRATCH/x.png --width 960 --height 540 --scene templates/dev.yaml` (~2 s, exits by itself; renders with the Export context's profile. `--profile Flat|Lensed|Traced|Reference`, `--set key=value`, `--list-settings`; a `.render.yaml` lands next to the image). Wrap in `timeout 90`. A timeout/hang here usually means a failed shader compile. `grep '\[error\]'` the output
2. **Test real interactivity** with `tools/ui_drive.py` (Hyprland + `/dev/uinput`, window-relative px; zsh needs `${=var}` or separate args):
   - `launch` (log in `.gpp/run.log`), `resize W H`, `click X Y [left|right|middle] [count]`, `drag X1 Y1 X2 Y2`, `scroll X Y N`, `key ctrl+z ...`, `shot out.png`, `alive`, `kill`, `tile`, `info`. `launch` records the PID in `.gpp/ui_drive.json` and every command targets that instance only (exports are never touched). `resize` floats the window. Do not run two MoleHole instances at once (shared hot-reload cache crashes them), so one GUI agent at a time
   - `alive` exits non-zero if no `Render stats: UI` line in the last 5 s = frozen UI thread. Run it after EVERY action, then `shot` and read the image
   - Minimum scenario for UI work: launch, resize small/large/tiny (60x60), click Play, click an outliner entity, `alive`, `shot`. Click once to hover, a click on a not-yet-hovered widget can be swallowed
   - Shader errors are only logged (`Shader reload failed`), the app keeps running with the old pipeline. Check the log after editing `shaders/`
3. **Hangs / crashes: use gdb, launched as the parent.** `ptrace_scope=1` forbids `gdb -p` on a running app. Run under it:
   `(sleep 25; pkill -INT -x MoleHole) & gdb -batch -ex 'set pagination off' -ex 'set debuginfod enabled off' -ex 'handle SIGUSR1 SIGUSR2 nostop noprint pass' -ex run -ex 'thread apply all bt 18' -ex kill ./build/MoleHole > $SCRATCH/gdb.txt`
   - ~30 threads, most idle (ThreadPool workers, PhysX, driver, timer). Find the odd ones: render/UI thread (`Renderer::RenderLoop`), sim thread (`SimulationRunner::ThreadMain`), main thread (SDL `PollEvents`, always idle-spinning)
   - A thread parked in a `std::condition_variable` wait is fine. A thread inside non-wait code at the same frame in two interrupts is the hang. Then `thread N`, `frame M`, `info locals` to get the values (root cause, not the symptom)
   - Frozen UI with a healthy main thread = render thread stuck in a layer's `OnUiRender` (plugin frame shows file:line). Frozen everything = look for mutex waits across threads (`__lll_lock_wait`) and compare the owners
   - `terminate called after throwing` = uncaught exception on a worker thread; the `what()` is in the output
   - Plugins are hot-reload `.so`s with debug info, breakpoints in them work after load
4. Persistent UI state lives in `.gpp/` (gitignored). Suspect it first when a hang/layout bug only happens on a second launch (`.gpp/node_editor.json`, `.gpp/imgui_main.ini`, `.gpp/molehole_state.yaml`); delete one file at a time to bisect

## Delegating (keep the main context small)
Big files (`animation_graph_window_layer.cpp` 2600 lines, `scene_window_layer.cpp` 1000) and multi-file features blow up the main context. Hand them to subagents (Agent tool) and keep only decisions and results in the main thread.
- Read-only questions ("where is X", "how does Y work") → Explore agent, not file dumps
- Implementation chunks → a general-purpose agent with a self-contained brief: goal, exact files, the rules in this file, build/verify steps, what not to touch, report format (≤ 300 words: files changed, evidence, open issues)
- Same working tree, so: disjoint file sets may run in parallel, agents that share files run one after the other. Build with `flock /tmp/molehole-build.lock cmake --build build` (one ninja per build dir); a build error in a file the agent did not touch = another agent is mid-edit, wait and retry
- Agents never commit or push; the main thread commits explicit paths when asked
- An agent's report is a claim: re-run the tests and look at one export or `ui_drive.py` screenshot before reporting done
- PROGRESS.md entries are written by the main thread from the reports

## Progress log (mandatory until the physics/render batch is done)
For ANY change that touches the physics/render plan (units, shaders, render, GR, scene schema, widgets for them, related bugs), in the SAME session, before your final answer:
1. Update `docs/physics-roadmap/PROGRESS.md`: tick/untick items, add new gaps and bugs you found (even if you did not fix them), add a dated line to the Log with what changed and test/verification status
2. Never tick an item unless it builds, tests pass, and you looked at an image export or a `tools/ui_drive.py` screenshot of it
3. Say in your final message which PROGRESS.md entries you changed. No entry = the work is not done
This rule ends when PROGRESS.md has no open items in the active phase and the user says so

## Docs
- `docs/physics-roadmap/PROGRESS.md` = live status of the physics/render plan. Update it with every change to that plan
- SI everywhere, constants only in `gpp/src/core/units/units.cppm`, non-SI only via named boundaries (see DEV_GUIDE "Units, constants, precision")
- `docs/USER_GUIDE.md`, `docs/DEV_GUIDE.md`, `docs/export-instructions.md`
