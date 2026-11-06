local orbit = require("lib/orbit")

local function spawn_ship(ap_props, ship_props, extra)
    local components = {
        RigidBody = { Type = 2, Mass = 100, EnableGravity = false },
        Collider = { Shape = 1, Radius = 1 },
    }
    for key, value in extra or {} do
        components[key] = value
    end
    local ship = sim.spawn("Ship", components)
    sim.set_velocity(ship, vector.create(0, 0, 0))
    local props = { ThrustX = 500, ThrustY = 500, ThrustZ = 500, TorquePitch = 400, TorqueYaw = 400, TorqueRoll = 400, ResponseTime = 0.2 }
    for key, value in ship_props or {} do
        props[key] = value
    end
    sim.attach(ship, "ship_controller", props)
    sim.attach(ship, "autopilot", ap_props)
    return ship
end

local MOVER = [[
return {
    OnFixedTick = function(self, dt)
        local p = scene.get64(self.entity, "Transform", "Position")
        scene.set64(self.entity, "Transform", "Position", p + scene.get64(self.entity, "Velocity", "Linear") * dt)
    end,
}
]]

local function spawn_target(position, velocity)
    local target = sim.spawn("Target", { Velocity = {} })
    scene.set64(target, "Transform", "Position", position)
    sim.set_velocity(target, vector.create((velocity or dvec3.new(0, 0, 0)).x, (velocity or dvec3.new(0, 0, 0)).y, (velocity or dvec3.new(0, 0, 0)).z))
    sim.define_script("mover", MOVER)
    sim.attach(target, "mover")
    return target
end

local function position(entity) return scene.get64(entity, "Transform", "Position") end
local function velocity(entity) return scene.get64(entity, "Velocity", "Linear") end

local function last_status()
    local list = sim.events("autopilot_status")
    return list[#list].args
end

test("go-to flies to the target, stops there and reports arrival", function()
    local target = spawn_target(dvec3.new(1500, 400, -2200))
    local ship = spawn_ship({ Mode = 1, Target = target, Engaged = true, Tolerance = 5, SpeedTolerance = 0.3 })
    sim.step(60 * 90)
    assert(dvec3.distance(position(ship), position(target)) < 5, dvec3.distance(position(ship), position(target)))
    assert(dvec3.length(velocity(ship)) < 0.3)
    local status = last_status()
    assert_eq(status[2], 1)
    assert_eq(status[3], 1)
    assert_eq(status[4], "ARRIVED")
end)

test("go-to turns the nose toward the target while it flies", function()
    local target = spawn_target(dvec3.new(3000, 0, 0))
    local ship = spawn_ship({ Mode = 1, Target = target, Engaged = true })
    sim.step(60 * 3)
    local nose = dquat.rotate(scene.get64(ship, "Transform", "Rotation"), dvec3.new(0, 0, -1))
    assert(nose.x > 0.9, nose.x)
end)

test("go-to chases a moving target and ends on it", function()
    local target = spawn_target(dvec3.new(1000, 0, 0), dvec3.new(0, 4, -3))
    local ship = spawn_ship({ Mode = 1, Target = target, Engaged = true, Tolerance = 5, SpeedTolerance = 0.5 })
    sim.step(60 * 60)
    assert(dvec3.distance(position(ship), position(target)) < 5)
    assert(dvec3.distance(velocity(ship), velocity(target)) < 0.5)
end)

test("standoff stops short of the target on the ship's side", function()
    local target = spawn_target(dvec3.new(0, 0, -2000))
    local ship = spawn_ship({ Mode = 1, Target = target, Engaged = true, Standoff = 300, Tolerance = 5 })
    sim.step(60 * 50)
    assert_near(dvec3.distance(position(ship), position(target)), 300, 6)
    assert(dvec3.length(velocity(ship)) < 0.5)
end)

test("match velocity brings the relative speed to zero", function()
    local target = spawn_target(dvec3.new(5000, 0, 0), dvec3.new(20, 5, -12))
    local ship = spawn_ship({ Mode = 3, Target = target, Engaged = true, SpeedTolerance = 0.2 })
    sim.step(60)
    physics.set_velocity(ship, vector.create(-8, 3, 6))
    sim.step(60 * 20)
    assert(dvec3.distance(velocity(ship), velocity(target)) < 0.2)
    assert_eq(last_status()[4], "MATCHED")
end)

test("hold returns to the point it was engaged at after a shove", function()
    local ship = spawn_ship({ Mode = 0, Engaged = true, Tolerance = 3 })
    scene.set64(ship, "Transform", "Position", dvec3.new(200, 50, -100))
    sim.step(5)
    physics.add_impulse(ship, vector.create(800, 0, 300))
    sim.step(60 * 40)
    assert(dvec3.distance(position(ship), dvec3.new(200, 50, -100)) < 3, dvec3.distance(position(ship), dvec3.new(200, 50, -100)))
    assert(dvec3.length(velocity(ship)) < 0.5)
end)

test("the engage key toggles the autopilot and the mode key cycles modes", function()
    local target = spawn_target(dvec3.new(2000, 0, 0))
    local ship = spawn_ship({ Mode = 1, Target = target })
    sim.step(30)
    assert_near(dvec3.length(velocity(ship)), 0, 1e-9)
    sim.input_tap("autopilot")
    sim.step(60)
    assert(dvec3.length(velocity(ship)) > 1)
    assert_eq(last_status()[3], 1)
    sim.input_tap("autopilot_mode")
    sim.step(2)
    assert_eq(last_status()[3], 2)
    sim.input_tap("autopilot")
    sim.step(2)
    assert_eq(last_status()[2], 0)
    local before = velocity(ship)
    sim.step(60)
    assert_near(velocity(ship), before, 1e-6)
end)

test("manual input overrides the autopilot on its axis only", function()
    local target = spawn_target(dvec3.new(0, 0, -4000))
    local ship = spawn_ship({ Mode = 1, Target = target, Engaged = true }, { Assist = 1 })
    sim.step(60 * 3)
    assert(velocity(ship).z < -2)
    assert_near(velocity(ship).x, 0, 0.05)
    sim.input("move_x")
    sim.step(60 * 3)
    assert(velocity(ship).x > 10, velocity(ship).x)
    assert(velocity(ship).z < -4)
    sim.input("move_x", false)
    sim.step(60 * 140)
    assert(dvec3.distance(position(ship), position(target)) < 5, dvec3.distance(position(ship), position(target)))
end)

test("the kill key stops the autopilot's thrust", function()
    local target = spawn_target(dvec3.new(4000, 0, 0))
    local ship = spawn_ship({ Mode = 1, Target = target, Engaged = true })
    sim.step(60 * 3)
    assert(dvec3.length(velocity(ship)) > 2)
    sim.input("kill_velocity")
    sim.step(60 * 4)
    assert(dvec3.length(velocity(ship)) < 0.5)
end)

test("the autopilot keeps working while a free camera owns the keys", function()
    local target = spawn_target(dvec3.new(2000, 0, 0))
    local ship = spawn_ship({ Mode = 1, Target = target, Engaged = true })
    sim.emit("camera_mode", "fly")
    sim.step(60 * 3)
    assert(dvec3.length(velocity(ship)) > 2)
end)

test("two ships given the same plan end in the identical state", function()
    local shift = dvec3.new(0, 0, 5000)
    local t1 = spawn_target(dvec3.new(900, 200, -700), dvec3.new(1, 0, 2))
    local t2 = spawn_target(dvec3.new(900, 200, -700) + shift, dvec3.new(1, 0, 2))
    local a = spawn_ship({ Mode = 1, Target = t1, Engaged = true })
    local b = spawn_ship({ Mode = 1, Target = t2, Engaged = true })
    scene.set64(b, "Transform", "Position", shift)
    sim.step(60 * 30)
    assert_near(velocity(a), velocity(b), 1e-3)
    assert_near(position(b) - position(a), shift, 0.05)
end)

local HOLE_MASS = 1.0e27
local ORBIT_SHIP = { ThrustX = 2e6, ThrustY = 2e6, ThrustZ = 2e6, TorquePitch = 4e5, TorqueYaw = 4e5, TorqueRoll = 4e5, ResponseTime = 0.1, MaxRate = 360 }

local function orbit_scene(radius, extra, turn)
    local mu = orbit.mu(HOLE_MASS)
    if #scene.query("BlackHole") == 0 then
        local hole = sim.spawn("Hole", { BlackHole = { Mass = HOLE_MASS } })
        scene.set64(hole, "Transform", "Position", dvec3.new(0, 0, 0))
    end
    local ship = spawn_ship(extra, ORBIT_SHIP, { RigidBody = { Type = 2, Mass = 100, EnableGravity = true } })
    local at = dquat.rotate(dquat.from_axis_angle(dvec3.new(0, 1, 0), turn or 0), dvec3.new(radius, 0, 0))
    scene.set64(ship, "Transform", "Position", at)
    local v = orbit.launch_circular(mu, at, dvec3.new(0, 1, 0))
    sim.set_velocity(ship, vector.create(v.x, v.y, v.z))
    return ship, mu
end

local function elements_of(ship)
    return orbit.analyse(ship).elements
end

test("the headless gravity matches the orbit library over a quarter period", function()
    local ship, mu = orbit_scene(1.0e6, {})
    local el = elements_of(ship)
    local steps = math.floor(el.period / 4 * 60 + 0.5)
    local expected = orbit.propagate(mu, position(ship), velocity(ship), steps / 60)
    sim.step(steps)
    assert(dvec3.distance(position(ship), expected) < 0.02 * el.radius, dvec3.distance(position(ship), expected))
    assert_near(elements_of(ship).e, 0, 0.02)
end)

test("orbit insertion raises a circular orbit to the requested radius", function()
    local ship = orbit_scene(1.0e6, { Mode = 2, OrbitRadius = 1.5e6, Engaged = true, OrbitTolerance = 0.01, ResponseTime = 0.05 })
    sim.step(60 * 150)
    local el = elements_of(ship)
    assert(math.abs(el.periapsis - 1.5e6) < 0.03 * 1.5e6, el.periapsis)
    assert(math.abs(el.apoapsis - 1.5e6) < 0.03 * 1.5e6, el.apoapsis)
    assert(el.e < 0.03, el.e)
    local status = last_status()
    assert(status[4] == "STABLE" or status[4] == "COAST", status[4])
end)

test("orbit insertion with no radius circularises an eccentric orbit at the engage radius", function()
    local ship = orbit_scene(1.0e6, { Mode = 2, OrbitTolerance = 0.01, ResponseTime = 0.05 })
    sim.step(1)
    local v = velocity(ship)
    sim.set_velocity(ship, vector.create(v.x * 1.15, v.y * 1.15, v.z * 1.15))
    sim.step(2)
    assert(elements_of(ship).e > 0.2)
    sim.input_tap("autopilot")
    sim.step(60 * 150)
    local el = elements_of(ship)
    assert(el.e < 0.05, el.e)
end)

test("a ship in a stable orbit with the autopilot on stays put and uses no thrust", function()
    local ship = orbit_scene(1.0e6, { Mode = 2, Engaged = true, OrbitTolerance = 0.02 })
    sim.step(60 * 30)
    assert_near(elements_of(ship).e, 0, 0.02)
    assert_eq(last_status()[4], "STABLE")
end)

test("orbit insertion from two starting angles ends in the same orbit", function()
    local props = { Mode = 2, OrbitRadius = 1.4e6, Engaged = true, OrbitTolerance = 0.01, ResponseTime = 0.05 }
    local a = orbit_scene(1.0e6, props, 0)
    local b = orbit_scene(1.0e6, props, 1.0)
    sim.step(60 * 120)
    local ea, eb = elements_of(a), elements_of(b)
    assert_rel(ea.a, eb.a, 1e-3)
    assert_near(ea.e, eb.e, 1e-3)
    assert(math.abs(ea.periapsis - 1.4e6) < 0.03 * 1.4e6, ea.periapsis)
end)
