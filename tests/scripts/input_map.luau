local input_map = require("lib/input_map")
local plain = require("lib/plain")

local function fake(values)
    return {
        axis = function(name) return values.axes[name] or 0 end,
        held = function(name) return values.held[name] == true end,
        pressed = function(name) return values.pressed[name] == true end,
    }
end

local function frame()
    return { axes = {}, held = {}, pressed = {} }
end

test("shape applies deadzone, expo, sensitivity and invert", function()
    assert_eq(input_map.shape(0.3), 0.3)
    assert_eq(input_map.shape(0.1, { deadzone = 0.2 }), 0)
    assert_eq(input_map.shape(-0.2, { deadzone = 0.2 }), 0)
    assert_near(input_map.shape(0.6, { deadzone = 0.2 }), 0.5, 1e-12)
    assert_near(input_map.shape(-0.6, { deadzone = 0.2 }), -0.5, 1e-12)
    assert_near(input_map.shape(0.5, { expo = 1 }), 0.125, 1e-12)
    assert_near(input_map.shape(0.5, { expo = 0.5 }), 0.5 * 0.5 + 0.5 * 0.125, 1e-12)
    assert_near(input_map.shape(0.5, { sensitivity = 3 }), 1.5, 1e-12)
    assert_near(input_map.shape(0.5, { invert = true }), -0.5, 1e-12)
    assert_eq(input_map.shape(1, { deadzone = 0.5, expo = 0.7 }), 1)
    assert_eq(input_map.shape(7), 1)
    assert_eq(input_map.shape(0, { invert = true }), 0)
end)

test("a deadzone of 1 or more cannot divide by zero", function()
    for _, deadzone in { 1, 2 } do
        local out = input_map.shape(1, { deadzone = deadzone })
        assert(out == out and math.abs(out) <= 1)
    end
end)

test("the shaped axis is odd, monotonic and continuous at the deadzone", function()
    local options = { deadzone = 0.15, expo = 0.4 }
    local previous = 0
    for i = 0, 100 do
        local x = i / 100
        local y = input_map.shape(x, options)
        assert_near(input_map.shape(-x, options), -y, 1e-15)
        assert(y >= previous - 1e-15, "monotonic at " .. x)
        previous = y
    end
    assert_near(input_map.shape(0.1500001, options), 0, 1e-5)
end)

test("stick_shape normalises diagonals and uses a radial deadzone", function()
    local x, y = input_map.stick_shape(1, 1)
    assert_near(math.sqrt(x * x + y * y), 1, 1e-12)
    assert_near(x, y, 1e-12)
    x, y = input_map.stick_shape(0.1, 0.1, { deadzone = 0.2 })
    assert_eq(x, 0)
    assert_eq(y, 0)
    x, y = input_map.stick_shape(0.14, 0.14, { deadzone = 0.2 })
    assert_eq(x, 0)
    x, y = input_map.stick_shape(0.6, 0, { deadzone = 0.2 })
    assert_near(x, 0.5, 1e-12)
    assert_eq(y, 0)
    x, y = input_map.stick_shape(0, 0.5, { invert_y = true, sensitivity = 2 })
    assert_eq(x, 0)
    assert_near(y, -1, 1e-12)
    x, y = input_map.stick_shape(0, 0)
    assert_eq(x + y, 0)
end)

test("axes read an action or a positive/negative pair", function()
    local state = input_map.new({ axes = {
        throttle = { action = "move_z", deadzone = 0.1 },
        roll = { positive = "roll_r", negative = "roll_l", sensitivity = 2 },
    } })
    local f = frame()
    local source = fake(f)
    f.axes.move_z = 0.05
    f.axes.roll_r = 1
    input_map.update(state, 0.1, source)
    assert_eq(input_map.axis(state, "throttle"), 0)
    assert_eq(input_map.axis(state, "roll"), 2)
    f.axes.roll_l = 1
    input_map.update(state, 0.1, source)
    assert_eq(input_map.axis(state, "roll"), 0)
    f.axes.roll_r = 0
    input_map.update(state, 0.1, source)
    assert_eq(input_map.axis(state, "roll"), -2)
end)

test("slew limits how fast an axis moves", function()
    local state = input_map.new({ axes = { throttle = { action = "move_z", slew = 2 } } })
    local f = frame()
    local source = fake(f)
    f.axes.move_z = 1
    input_map.update(state, 0.25, source)
    assert_eq(input_map.axis(state, "throttle"), 0.5)
    input_map.update(state, 0.25, source)
    assert_eq(input_map.axis(state, "throttle"), 1)
    input_map.update(state, 0.25, source)
    assert_eq(input_map.axis(state, "throttle"), 1)
    f.axes.move_z = -1
    input_map.update(state, 0.5, source)
    assert_eq(input_map.axis(state, "throttle"), 0)
end)

test("toggles flip on the press edge only", function()
    local state = input_map.new({ toggles = { lights = { action = "light" }, assist = { action = "assist", initial = true } } })
    assert_eq(input_map.toggled(state, "lights"), false)
    assert_eq(input_map.toggled(state, "assist"), true)
    local f = frame()
    local source = fake(f)
    f.pressed.light = true
    f.held.light = true
    input_map.update(state, 0.1, source)
    assert_eq(input_map.toggled(state, "lights"), true)
    f.pressed.light = false
    input_map.update(state, 0.1, source)
    input_map.update(state, 0.1, source)
    assert_eq(input_map.toggled(state, "lights"), true)
    f.pressed.light = true
    input_map.update(state, 0.1, source)
    assert_eq(input_map.toggled(state, "lights"), false)
    input_map.set_toggle(state, "lights", true)
    assert_eq(input_map.toggled(state, "lights"), true)
    assert_eq(input_map.toggled(state, "assist"), true)
end)

test("hold-to-repeat fires on press, after the delay, then at the rate", function()
    local state = input_map.new({ repeats = { fire = { action = "fire", delay = 0.5, rate = 4 } } })
    local f = frame()
    local source = fake(f)
    local counts = {}
    f.pressed.fire = true
    f.held.fire = true
    input_map.update(state, 0.25, source)
    table.insert(counts, input_map.repeats(state, "fire"))
    f.pressed.fire = false
    for i = 1, 4 do
        input_map.update(state, 0.25, source)
        table.insert(counts, input_map.repeats(state, "fire"))
    end
    assert_eq(counts, { 1, 0, 1, 1, 1 })
    f.held.fire = false
    input_map.update(state, 0.25, source)
    assert_eq(input_map.repeats(state, "fire"), 0)
    f.held.fire = true
    f.pressed.fire = true
    input_map.update(state, 0.25, source)
    assert_eq(input_map.repeats(state, "fire"), 1)
end)

test("a long frame fires every missed repeat", function()
    local state = input_map.new({ repeats = { fire = { action = "fire", delay = 0.5, rate = 4 } } })
    local f = frame()
    local source = fake(f)
    f.pressed.fire = true
    f.held.fire = true
    input_map.update(state, 0.1, source)
    f.pressed.fire = false
    input_map.update(state, 1, source)
    assert_eq(input_map.repeats(state, "fire"), 3)
end)

test("a tap fires once and does not stick", function()
    local state = input_map.new({ repeats = { fire = { action = "fire" } } })
    local f = frame()
    local source = fake(f)
    f.pressed.fire = true
    input_map.update(state, 0.1, source)
    assert_eq(input_map.repeats(state, "fire"), 1)
    f.pressed.fire = false
    input_map.update(state, 0.1, source)
    assert_eq(input_map.repeats(state, "fire"), 0)
end)

test("combined sticks read two actions", function()
    local state = input_map.new({ sticks = { move = { x = "move_x", y = "move_z", deadzone = 0.1 } } })
    local f = frame()
    f.axes.move_x = 1
    f.axes.move_z = -1
    input_map.update(state, 0.1, fake(f))
    local x, y = input_map.stick(state, "move")
    assert_near(x, math.sqrt(0.5), 1e-12)
    assert_near(y, -math.sqrt(0.5), 1e-12)
end)

test("unknown names fail loudly", function()
    local state = input_map.new({ axes = { a = { action = "x" } } })
    expect_error(function() input_map.axis(state, "b") end, "unknown axis 'b'")
    expect_error(function() input_map.stick(state, "b") end, "unknown stick 'b'")
    expect_error(function() input_map.toggled(state, "b") end, "unknown toggle 'b'")
    expect_error(function() input_map.repeats(state, "b") end, "unknown repeat 'b'")
    expect_error(function() input_map.set_toggle(state, "b", true) end, "unknown toggle 'b'")
end)

local function everything()
    return input_map.new({
        axes = { throttle = { action = "move_z", deadzone = 0.1, expo = 0.3, slew = 3 }, roll = { positive = "move_x", negative = "move_y" } },
        toggles = { boost_lock = { action = "boost" } },
        repeats = { fire = { action = "fire", delay = 0.2, rate = 8 } },
        sticks = { move = { x = "move_x", y = "move_z", deadzone = 0.2, expo = 0.5 } },
    })
end

local function drive(state, ticks, start)
    local seen = {}
    for i = start, start + ticks - 1 do
        local f = frame()
        f.axes.move_z = math.sin(i / 7)
        f.axes.move_x = math.cos(i / 11)
        f.held.fire = i % 40 < 25
        f.pressed.fire = i % 40 == 0
        f.pressed.boost = i % 53 == 0
        input_map.update(state, 1 / 64, fake(f))
        local sx, sy = input_map.stick(state, "move")
        table.insert(seen, { input_map.axis(state, "throttle"), input_map.axis(state, "roll"), input_map.toggled(state, "boost_lock"), input_map.repeats(state, "fire"), sx, sy })
    end
    return seen
end

test("state is plain data and a copy continues identically", function()
    local state = everything()
    assert(plain.check(state))
    drive(state, 100, 0)
    local ok, where = plain.check(state)
    assert(ok, where)
    local copy = plain.copy(state)
    assert_eq(drive(state, 200, 100), drive(copy, 200, 100))
    assert_eq(state, copy)
end)

local PROBE = [[
local input_map = require("lib/input_map")
return {
    OnStart = function(self)
        self.map = input_map.new({
            axes = { throttle = { action = "move_z", deadzone = 0.2 } },
            toggles = { lights = { action = "fire" } },
            repeats = { jump = { action = "jump", delay = 0.1, rate = 20 } },
            sticks = { move = { x = "move_x", y = "move_z" } },
        })
    end,
    OnTick = function(self, dt)
        input_map.update(self.map, dt)
        local sx, sy = input_map.stick(self.map, "move")
        scene.set_xyz(self.entity, "Transform", "Position", input_map.axis(self.map, "throttle"), sx, sy)
        scene.set_xyz(self.entity, "Transform", "Scale", input_map.toggled(self.map, "lights") and 1 or 0, input_map.repeats(self.map, "jump"), 1)
    end,
}
]]

test("the map reads the engine input actions every tick", function()
    sim.define_script("map_probe", PROBE)
    local e = sim.spawn("Probe")
    sim.attach(e, "map_probe")
    sim.step(1)
    sim.input("move_z", 0.1)
    sim.step(1)
    assert_eq(scene.get_xyz(e, "Transform", "Position"), 0)
    sim.input("move_z", 0.6)
    sim.input("move_x", 0)
    sim.step(1)
    local axis, sx, sy = scene.get_xyz(e, "Transform", "Position")
    assert_near(axis, 0.5, 1e-6)
    assert_near(sx, 0, 1e-6)
    assert_near(sy, 0.6, 1e-6)
    sim.input_tap("fire")
    sim.step(1)
    assert_eq(scene.get_xyz(e, "Transform", "Scale"), 1)
    sim.step(3)
    assert_eq(scene.get_xyz(e, "Transform", "Scale"), 1)
    sim.input_tap("fire")
    sim.step(1)
    assert_eq(scene.get_xyz(e, "Transform", "Scale"), 0)
    sim.input("jump")
    sim.step(1)
    local _, jumps = scene.get_xyz(e, "Transform", "Scale")
    assert_eq(jumps, 1)
    local total = 0
    for i = 1, 30 do
        sim.step(1)
        local _, c = scene.get_xyz(e, "Transform", "Scale")
        total += c
    end
    assert(total >= 5 and total <= 12, "repeats " .. total)
end)

test("two scripts with the same input history agree", function()
    sim.define_script("map_probe", PROBE)
    local a, b = sim.spawn("A"), sim.spawn("B")
    sim.attach(a, "map_probe")
    sim.attach(b, "map_probe")
    for i = 1, 90 do
        sim.input("move_z", math.sin(i / 5))
        sim.input("move_x", math.cos(i / 3))
        if i % 17 == 0 then sim.input_tap("fire") end
        sim.input("jump", i % 30 < 20)
        sim.step(1)
    end
    assert_eq(scene.get(a, "Transform", "Position"), scene.get(b, "Transform", "Position"))
    assert_eq(scene.get(a, "Transform", "Scale"), scene.get(b, "Transform", "Scale"))
end)
