local controls = require("lib/controls")

local function texts()
    local list = {}
    for _, command in sim.hud() do
        if command.kind == "text" then
            table.insert(list, command.text)
        end
    end
    return list
end

local function has(needle)
    for _, text in texts() do
        if string.find(text, needle, 1, true) then
            return true
        end
    end
    return false
end

local function bind_scene()
    sim.input_bind("move_z", "key:w", "-key:s")
    sim.input_bind("move_x", "key:d", "-key:a")
    sim.input_bind("pitch", "key:up", "-key:down", "key:i", "-key:k")
    sim.input_bind("boost", "key:shift")
    sim.input_bind("throttle", "key:z", "-key:x")
    sim.input_bind("camera", "key:c")
    sim.input_bind("controls", "key:f1")
end

local function spawn_hud()
    sim.spawn("Camera", { Camera = { Primary = true, Fov = 60 } })
    local ship = sim.spawn("Ship", {
        RigidBody = { Type = 2, Mass = 100, EnableGravity = false },
        Collider = { Shape = 1, Radius = 1 },
    })
    sim.attach(ship, "telemetry_hud")
    sim.input_bind("hud", "key:h")
    return ship
end

local function step_to(seconds)
    while time.elapsed() < seconds do
        sim.step(10)
    end
end

test("key labels name keys, punctuation and mouse buttons", function()
    assert_eq(controls.key_label("key:w"), "W")
    assert_eq(controls.key_label("key:space"), "Space")
    assert_eq(controls.key_label("key:period"), ".")
    assert_eq(controls.key_label("key:f1"), "F1")
    assert_eq(controls.key_label("mouse:left"), "LMB")
end)

test("descriptions put positive keys first and negative keys after a slash", function()
    assert_eq(controls.describe({ "key:w", "-key:s" }), "W / S")
    assert_eq(controls.describe({ "key:up", "-key:down", "key:i", "-key:k" }), "Up, I / Down, K")
    assert_eq(controls.describe({ "key:f" }), "F")
    assert_eq(controls.describe({ "0.5*key:x", "-2*key:y" }), "X / Y")
    assert_eq(controls.describe({}), nil)
end)

test("collect lists only bound actions in group order", function()
    local bound = {
        move_z = { "key:w", "-key:s" },
        camera = { "key:c" },
        hud = {},
    }
    local shown = controls.collect(function(action)
        return bound[action] and controls.describe(bound[action])
    end)
    assert_eq(#shown, 2)
    assert_eq(shown[1].title, "THRUST")
    assert_eq(shown[1].rows[1].keys, "W / S")
    assert_eq(shown[2].title, "CAMERA")
end)

test("nothing about capture is drawn when the host reports no input source", function()
    spawn_hud()
    sim.step(3)
    assert(not has("Click the viewport"))
    assert(not has("THRUST"))
end)

test("the capture hint shows while input is released and goes away once captured", function()
    spawn_hud()
    bind_scene()
    sim.input_captured(false)
    sim.step(3)
    assert(has("Click the viewport to capture controls"))
    assert(has("F1 shows the key list"))
    assert(not has("THRUST"))
    sim.input_captured(true)
    sim.step(2)
    assert(not has("Click the viewport"))
end)

test("the panel lists the keys bound in the scene", function()
    spawn_hud()
    bind_scene()
    sim.input_captured(true)
    sim.step(3)
    assert(has("THRUST"))
    assert(has("Forward / back"))
    assert(has("W / S"))
    assert(has("D / A"))
    assert(has("Up, I / Down, K"))
    assert(has("Shift"))
    assert(has("Z / X"))
    assert(has("CAMERA"))
    assert(not has("Autopilot mode"))
    assert(not has("Kill velocity"))
end)

test("rebinding changes the panel", function()
    spawn_hud()
    bind_scene()
    sim.input_captured(true)
    sim.step(2)
    assert(has("W / S"))
    sim.input_bind("move_z", "key:t")
    sim.step(2)
    assert(has("W, T / S"))
end)

test("the panel collapses to a hint after ten seconds of sim time", function()
    spawn_hud()
    bind_scene()
    sim.input_captured(true)
    sim.step(2)
    step_to(controls.AUTO_SECONDS - 1)
    sim.step(1)
    assert(has("Forward / back"))
    assert(not has("F1 controls"))
    step_to(controls.AUTO_SECONDS + 0.5)
    sim.step(1)
    assert(not has("Forward / back"))
    assert(has("F1 controls"))
end)

test("the controls key toggles the panel at any time", function()
    spawn_hud()
    bind_scene()
    sim.input_captured(true)
    sim.step(2)
    assert(has("Forward / back"))
    sim.input_tap("controls")
    sim.step(1)
    assert(not has("Forward / back"))
    assert(has("F1 controls"))
    sim.input_tap("controls")
    sim.step(1)
    assert(has("Forward / back"))
    step_to(controls.AUTO_SECONDS + 5)
    sim.step(1)
    assert(has("Forward / back"))
    sim.input_tap("controls")
    sim.step(1)
    assert(not has("Forward / back"))
end)

test("the panel stays collapsed after releasing and capturing again", function()
    spawn_hud()
    bind_scene()
    sim.input_captured(true)
    step_to(controls.AUTO_SECONDS + 1)
    sim.step(1)
    assert(not has("Forward / back"))
    sim.input_captured(false)
    sim.step(1)
    assert(has("Click the viewport"))
    sim.input_captured(true)
    sim.step(2)
    assert(not has("Forward / back"))
    assert(has("F1 controls"))
end)

test("the HUD off state hides the panel but keeps the capture hint", function()
    spawn_hud()
    bind_scene()
    sim.input_captured(true)
    sim.step(2)
    assert(has("Forward / back"))
    sim.input_tap("hud")
    sim.step(1)
    sim.input_tap("hud")
    sim.step(1)
    assert(not has("Forward / back"))
    assert(not has("SPEED"))
    sim.input_captured(false)
    sim.step(1)
    assert(has("Click the viewport to capture controls"))
end)

test("the panel reflows into fewer columns on a narrow view", function()
    local h, calls = {}, {}
    for _, name in { "text", "line", "rect", "circle", "arc" } do
        h[name] = function(...)
            table.insert(calls, { name = name, ... })
        end
    end
    local shown = controls.collect(function()
        return "A"
    end)
    controls.draw(h, shown, { size = 15, aspect = 1.0, top = 0.2 })
    local narrow = {}
    for _, call in calls do
        if call.name == "text" and call[4].align == nil then
            narrow[call[1]] = true
        end
    end
    local columns = 0
    for _ in narrow do
        columns += 1
    end
    assert_eq(columns, 2)
end)
