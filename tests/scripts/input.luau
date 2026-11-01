local READER = [[
return {
    OnTick = function(self, dt)
        local m = input.mouse_delta()
        local e = self.entity
        scene.set_xyz(e, "Transform", "Position",
            input.axis("move_x"),
            input.held("fire") and 1 or 0,
            (input.pressed("fire") and 1 or 0) + (input.released("fire") and 2 or 0))
        scene.set_xyz(e, "Transform", "Scale", m.x, m.y, 1)
    end,
}
]]

local function setup()
    sim.define_script("reader", READER)
    local entity = sim.spawn("Reader")
    sim.attach(entity, "reader")
    return entity
end

local function pos(e) return scene.get_xyz(e, "Transform", "Position") end

test("input reads zero until something is injected", function()
    local e = setup()
    sim.step(2)
    local x, held, edges = pos(e)
    assert_eq(x, 0)
    assert_eq(held, 0)
    assert_eq(edges, 0)
end)

test("axes and held actions persist across ticks", function()
    local e = setup()
    sim.input("move_x", -0.5)
    sim.input("fire")
    sim.step(3)
    local x, held = pos(e)
    assert_near(x, -0.5)
    assert_eq(held, 1)
    sim.input("fire", false)
    sim.step(1)
    local _, after = pos(e)
    assert_eq(after, 0)
end)

test("pressed and released fire for one tick only", function()
    local e = setup()
    sim.input("fire", true)
    sim.step(1)
    local _, _, edges = pos(e)
    assert_eq(edges, 1)
    sim.step(1)
    _, _, edges = pos(e)
    assert_eq(edges, 0)
    sim.input("fire", false)
    sim.step(1)
    _, _, edges = pos(e)
    assert_eq(edges, 2)
    sim.step(1)
    _, _, edges = pos(e)
    assert_eq(edges, 0)
end)

test("a tap between ticks is not lost", function()
    local e = setup()
    sim.input_tap("fire")
    sim.step(1)
    local _, held, edges = pos(e)
    assert_eq(held, 0)
    assert_eq(edges, 3)
end)

test("mouse delta is consumed by the tick that sees it", function()
    local e = setup()
    sim.input_mouse(4, -2)
    sim.input_mouse(1, 0)
    sim.step(1)
    local sx, sy = scene.get_xyz(e, "Transform", "Scale")
    assert_eq(sx, 5)
    assert_eq(sy, -2)
    sim.step(1)
    sx, sy = scene.get_xyz(e, "Transform", "Scale")
    assert_eq(sx, 0)
    assert_eq(sy, 0)
end)

test("input_clear drops held actions", function()
    local e = setup()
    sim.input("move_x", 1)
    sim.step(1)
    sim.input_clear()
    sim.step(1)
    assert_eq(pos(e), 0)
end)
