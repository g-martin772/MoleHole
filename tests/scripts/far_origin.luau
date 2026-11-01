local ORIGIN = dvec3.new(1e11, -2.5e11, 3e11)
local VELOCITY = dvec3.new(29780.5, -1234.125, 77.0625)
local TICKS = 300
local DT = 1 / 64

local NARROW = [[
return {
    OnTick = function(self, dt)
        local p = scene.get(self.entity, "Transform", "Position")
        scene.set(self.entity, "Transform", "Position", p + vector.create(29780.5, -1234.125, 77.0625) * dt)
    end,
}
]]

local WIDE_ONLY = [[
return {
    OnTick = function(self, dt)
        local p = scene.get64(self.entity, "Transform", "Position")
        scene.set64(self.entity, "Transform", "Position", p + dvec3.new(29780.5, -1234.125, 77.0625) * dt)
    end,
}
]]

local function expected(start)
    return start + VELOCITY * (TICKS * DT)
end

test("a body at 1e11 m integrated with get64/set64 stays within a millimetre", function()
    sim.set_dt(DT)
    sim.define_script("wide", WIDE_ONLY)
    local ship = sim.spawn("Ship", { Transform = { Position = ORIGIN } })
    sim.attach(ship, "wide")
    sim.step(TICKS)
    local p = scene.get64(ship, "Transform", "Position")
    local error = dvec3.distance(p, expected(ORIGIN))
    assert(error < 1e-3, "error " .. error .. " m")
end)

test("the float32 path loses kilometres over the same run", function()
    sim.set_dt(DT)
    sim.define_script("narrow", NARROW)
    local ship = sim.spawn("Ship", { Transform = { Position = ORIGIN } })
    sim.attach(ship, "narrow")
    sim.step(TICKS)
    local p = scene.get64(ship, "Transform", "Position")
    local error = dvec3.distance(p, expected(ORIGIN))
    assert(error > 1000, "float32 error only " .. error .. " m")
end)

test("wide and narrow bodies side by side show the gap", function()
    sim.set_dt(DT)
    sim.define_script("wide", WIDE_ONLY)
    sim.define_script("narrow", NARROW)
    local good = sim.spawn("Good", { Transform = { Position = ORIGIN } })
    local bad = sim.spawn("Bad", { Transform = { Position = ORIGIN } })
    sim.attach(good, "wide")
    sim.attach(bad, "narrow")
    sim.step(TICKS)
    local truth = expected(ORIGIN)
    local good_error = dvec3.distance(scene.get64(good, "Transform", "Position"), truth)
    local bad_error = dvec3.distance(scene.get64(bad, "Transform", "Position"), truth)
    assert(good_error < 1e-3)
    assert(bad_error > 1e5 * good_error or good_error == 0, "bad " .. bad_error .. " good " .. good_error)
end)

test("get returns the float32 rounding of what get64 returns", function()
    local ship = sim.spawn("Ship", { Transform = { Position = ORIGIN + dvec3.new(1e-3, 0, 0) } })
    local wide = scene.get64(ship, "Transform", "Position")
    local narrow = scene.get(ship, "Transform", "Position")
    assert_eq(dvec3.to_vec(wide), narrow)
    assert(math.abs(narrow.x - wide.x) > 100, "float32 cannot hold the offset")
    assert(math.abs(wide.x - (ORIGIN.x + 1e-3)) == 0)
end)

test("a script reads its own far position, offsets it by a millimetre and the scene holds it", function()
    local ship = sim.spawn("Ship", { Transform = { Position = ORIGIN } })
    local start = scene.get64(ship, "Transform", "Position")
    for i = 1, 10 do
        scene.set64(ship, "Transform", "Position", scene.get64(ship, "Transform", "Position") + dvec3.new(1e-3, 0, 0))
        sim.step(1)
    end
    local delta = scene.get64(ship, "Transform", "Position") - start
    assert(math.abs(delta.x - 1e-2) < 1e-4, "delta " .. delta.x)
end)

test("a physics body drifting at 1e11 m is read back to sub-centimetre accuracy", function()
    sim.set_dt(DT)
    local ship = sim.spawn("Ship", {
        Transform = { Position = ORIGIN },
        RigidBody = { Type = 2, Mass = 2, EnableGravity = false },
        Collider = { Shape = 1, Radius = 0.5 },
    })
    sim.set_velocity(ship, vector.create(0, 0, 0))
    sim.step(1)
    physics.set_velocity(ship, dvec3.new(0, 0, 10))
    sim.step(128)
    local p = scene.get64(ship, "Transform", "Position")
    local drift = p.z - ORIGIN.z
    assert(math.abs(drift - 10 * 128 * DT) < 0.05, "drift " .. drift)
    assert(math.abs(p.x - ORIGIN.x) < 1e-3)
    assert_eq(typeof(scene.get64(ship, "Velocity", "Linear")), "dvec3")
    assert_near(scene.get64(ship, "Velocity", "Linear").z, 10, 1e-3)
end)
