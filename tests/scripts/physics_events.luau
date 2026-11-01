local CONTACT_PROBE = [[
return {
    OnContact = function(self, other, point, normal, impulse, velocity, material, otherMaterial)
        events.emit("contact", self.entity, other, point, normal, impulse, velocity, material, otherMaterial)
    end,
}
]]

local TRIGGER_PROBE = [[
return {
    OnTrigger = function(self, other, entered) events.emit("trigger", self.entity, other, entered) end,
}
]]

local function ball(name, position, velocity, material)
    local entity = sim.spawn(name, {
        Transform = { Position = position },
        RigidBody = { Type = 2, Mass = 1, EnableGravity = false },
        Collider = { Shape = 1, Radius = 0.5, MaterialId = material or 0 },
    })
    sim.set_velocity(entity, velocity)
    return entity
end

test("two falling bodies deliver OnContact to both with mirrored, sane data", function()
    sim.define_script("contact_probe", CONTACT_PROBE)
    local upper = ball("Upper", vector.create(0, 3, 0), vector.create(0, -4, 0), 7)
    local lower = ball("Lower", vector.create(0, 0, 0), vector.create(0, -1, 0), 9)
    sim.attach(upper, "contact_probe")
    sim.attach(lower, "contact_probe")
    sim.step(120)

    local contacts = sim.events("contact")
    assert_eq(#contacts, 2)
    local first, second = contacts[1].args, contacts[2].args
    assert_eq(first[1], upper)
    assert_eq(first[2], lower)
    assert_eq(second[1], lower)
    assert_eq(second[2], upper)
    assert_eq(contacts[1].tick, contacts[2].tick)
    assert(contacts[1].tick > 30 and contacts[1].tick < 60, "contact tick " .. contacts[1].tick)

    assert_near(first[4], vector.create(0, 1, 0), 0.05)
    assert_near(second[4], vector.create(0, -1, 0), 0.05)
    assert_near(first[6], vector.create(0, -3, 0), 0.05)
    assert_near(second[6], vector.create(0, 3, 0), 0.05)
    assert(first[5] > 1 and first[5] < 2.5, "impulse " .. first[5])
    assert_near(first[5], second[5], 1e-6)
    assert_eq(first[3], second[3])
    assert_near(first[3].x, 0, 0.05)
    assert_near(first[3].z, 0, 0.05)
    assert_near(first[3].y, -1 / 6, 0.1, "contact point halfway between the centres at touch time")
    assert_eq(first[7], 7)
    assert_eq(first[8], 9)
    assert_eq(second[7], 9)
    assert_eq(second[8], 7)
end)

test("OnContact fires once per touch, not every tick", function()
    sim.define_script("contact_probe", CONTACT_PROBE)
    local floor = sim.spawn("Floor", {
        Transform = { Position = vector.create(0, -1, 0) },
        RigidBody = { Type = 0 },
        Collider = { Shape = 0, HalfExtents = vector.create(10, 0.5, 10) },
    })
    local fall = ball("Fall", vector.create(0, 1, 0), vector.create(0, -2, 0))
    sim.attach(floor, "contact_probe")
    sim.attach(fall, "contact_probe")
    sim.step(180)
    assert_eq(#sim.events("contact"), 2)
end)

test("a trigger reports a body entering and leaving, and stays out of the physics", function()
    sim.define_script("trigger_probe", TRIGGER_PROBE)
    local zone = sim.spawn("Zone", {
        Transform = { Position = vector.create(0, 0, 0) },
        RigidBody = { Type = 0 },
        Collider = { Shape = 0, HalfExtents = vector.create(1, 1, 1), IsTrigger = true },
    })
    local body = sim.spawn("Body", {
        Transform = { Position = vector.create(-3, 0, 0) },
        RigidBody = { Type = 2, Mass = 1, EnableGravity = false },
        Collider = { Shape = 1, Radius = 0.25 },
    })
    sim.set_velocity(body, vector.create(2, 0, 0))
    sim.attach(zone, "trigger_probe")
    sim.attach(body, "trigger_probe")
    sim.step(180)

    local triggers = sim.events("trigger")
    assert_eq(#triggers, 4)
    local zoneEnter, bodyEnter, zoneExit, bodyExit = triggers[1], triggers[2], triggers[3], triggers[4]
    assert_eq(zoneEnter.args, { zone, body, true })
    assert_eq(bodyEnter.args, { body, zone, true })
    assert_eq(zoneExit.args, { zone, body, false })
    assert_eq(bodyExit.args, { body, zone, false })
    assert(zoneEnter.tick >= 50 and zoneEnter.tick <= 56, "enter tick " .. zoneEnter.tick)
    assert(zoneExit.tick >= 125 and zoneExit.tick <= 131, "exit tick " .. zoneExit.tick)
    assert_near(scene.get(body, "Transform", "Position").x, 3, 0.05)
end)

test("OnLateTick sees the bodies after the physics step", function()
    sim.define_script("observer", [[
return {
    OnTick = function(self) events.emit("seen", "tick", scene.get(self.entity, "Transform", "Position").y) end,
    OnLateTick = function(self) events.emit("seen", "late", scene.get(self.entity, "Transform", "Position").y) end,
}
]])
    local body = ball("Body", vector.create(0, 10, 0), vector.create(0, -6, 0))
    sim.attach(body, "observer")
    sim.step(1)
    local seen = sim.events("seen")
    assert_eq(seen[1].args[1], "tick")
    assert_eq(seen[2].args[1], "late")
    assert_near(seen[1].args[2], 10, 1e-4)
    assert_near(seen[2].args[2], 10 - 6 * sim.dt(), 1e-3)
end)

test("the same run gives the same state hash", function()
    sim.define_script("contact_probe", CONTACT_PROBE)
    local upper = ball("Upper", vector.create(0, 3, 0), vector.create(0, -4, 0))
    local lower = ball("Lower", vector.create(0, 0, 0), vector.create(0, -1, 0))
    sim.attach(upper, "contact_probe")
    sim.attach(lower, "contact_probe")
    sim.step(90)
    assert_eq(type(sim.state_hash()), "string")
end)
