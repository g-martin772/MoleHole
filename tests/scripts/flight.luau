local flight = require("lib/flight")
local plain = require("lib/plain")

local DT = 1 / 60

local function body(overrides)
    local b = {
        mass = 100,
        rotation = dquat.new(0, 0, 0, 1),
        velocity = dvec3.new(0, 0, 0),
        angular_velocity = dvec3.new(0, 0, 0),
    }
    for key, value in overrides or {} do
        b[key] = value
    end
    return b
end

local function ship(config)
    local entity = sim.spawn("Ship", {
        RigidBody = { Type = 2, Mass = 100, EnableGravity = false },
        Collider = { Shape = 1, Radius = 1 },
    })
    sim.set_velocity(entity, vector.create(0, 0, 0))
    local state = flight.new(config)
    return entity, state
end

local function fly(entity, state, command, ticks)
    for _ = 1, ticks do
        flight.apply(entity, state, command, DT * time.scale())
        sim.step(1)
    end
end

local function speed(entity)
    return dvec3.length(scene.get64(entity, "Velocity", "Linear"))
end

local function spin(entity)
    return dvec3.length(scene.get64(entity, "Velocity", "Angular"))
end

test("thrust is the axis limit along the ship's own axes", function()
    local state = flight.new({ thrust_x = 100, thrust_y = 200, thrust_z = 300, assist = flight.OFF })
    local force = flight.step(state, body(), { x = 1, y = 0, z = 0 }, DT)
    assert_near(force.x, 100, 1e-9)
    assert_near(force.y, 0, 1e-9)
    force = flight.step(state, body(), { y = -1 }, DT)
    assert_near(force.y, -200, 1e-9)
    force = flight.step(state, body(), { z = 1 }, DT)
    assert_near(force.z, -300, 1e-9)
    force = flight.step(state, body(), { z = -0.5 }, DT)
    assert_near(force.z, 150, 1e-9)
end)

test("thrust follows the ship's rotation", function()
    local state = flight.new({ thrust_z = 300, assist = flight.OFF })
    local turned = dquat.from_axis_angle(dvec3.new(0, 1, 0), math.pi / 2)
    local force = flight.step(state, body({ rotation = turned }), { z = 1 }, DT)
    assert_near(force.x, -300, 1e-9)
    assert_near(force.z, 0, 1e-9)
end)

test("a thrusting ship accelerates by F over m", function()
    local entity, state = ship({ thrust_z = 500, assist = flight.OFF })
    fly(entity, state, { z = 1 }, 60)
    assert_near(scene.get64(entity, "Velocity", "Linear"), dvec3.new(0, 0, -5), 1e-3)
    assert_near(scene.get64(entity, "Transform", "Position").z, -2.5 - 5 * DT / 2, 0.01)
end)

test("mass override changes the acceleration, not the thrust", function()
    local entity, state = ship({ thrust_z = 500, mass = 50, assist = flight.OFF })
    fly(entity, state, { z = 1 }, 60)
    assert_near(speed(entity), 10, 1e-3)
    local info = select(3, flight.step(state, body(), { z = 1 }, DT))
    assert_near(info.accel, 10, 1e-9)
end)

test("the G limit clamps the thrust acceleration", function()
    local state = flight.new({ thrust_x = 5000, thrust_y = 5000, thrust_z = 5000, g_limit = 1, assist = flight.OFF })
    local force, _, info = flight.step(state, body(), { x = 1, y = 1, z = 1 }, DT)
    assert_near(dvec3.length(force) / 100, flight.G, 1e-9)
    assert(info.clamped)
    assert_near(info.g, 1, 1e-9)
    force, _, info = flight.step(state, body(), { z = 0.01 }, DT)
    assert(not info.clamped)
    assert_near(info.accel, 0.5, 1e-9)
    state.g_limit = 0
    force = flight.step(state, body(), { x = 1, y = 1, z = 1 }, DT)
    assert_near(dvec3.length(force), 5000 * math.sqrt(3), 1e-6)
end)

test("boost scales the thrust and still respects the G limit", function()
    local state = flight.new({ thrust_z = 100, boost = 3, assist = flight.OFF })
    local force = flight.step(state, body(), { z = 1, boost = true }, DT)
    assert_near(force.z, -300, 1e-9)
    state.g_limit = 2
    local _, _, info = flight.step(state, body(), { z = 1, boost = true }, DT)
    assert(not info.clamped)
    state.g_limit = 0.1
    _, _, info = flight.step(state, body(), { z = 1, boost = true }, DT)
    assert(info.clamped and info.boosted)
    assert_near(info.accel, 0.1 * flight.G, 1e-9)
end)

test("assist off applies raw torque only while a rotation is commanded", function()
    local state = flight.new({ torque_x = 10, torque_y = 20, torque_z = 30, assist = flight.OFF })
    local spinning = body({ angular_velocity = dvec3.new(0, 0, 3) })
    local _, torque = flight.step(state, spinning, {}, DT)
    assert_eq(dvec3.length(torque), 0)
    _, torque = flight.step(state, spinning, { pitch = 1, yaw = 1, roll = 1 }, DT)
    assert_near(torque, dvec3.new(10, -20, -30), 1e-9)
end)

test("rate damping pushes against the spin", function()
    local state = flight.new({ assist = flight.RATE, inertia_x = 40, inertia_y = 40, inertia_z = 40 })
    local _, torque = flight.step(state, body({ angular_velocity = dvec3.new(0.5, -0.5, 1) }), {}, DT)
    assert(torque.x < 0 and torque.y > 0 and torque.z < 0)
end)

test("rate damping brings a spinning body to rest", function()
    local entity, state = ship({ assist = flight.RATE, torque_x = 400, torque_y = 400, torque_z = 400, response = 0.2 })
    sim.step(1)
    physics.set_angular_velocity(entity, vector.create(1, -2, 1.5))
    sim.step(1)
    local before = spin(entity)
    assert(before > 2.4)
    fly(entity, state, {}, 120)
    assert(spin(entity) < before * 0.05, "spin " .. spin(entity))
end)

test("assist off leaves the spin alone", function()
    local entity, state = ship({ assist = flight.OFF })
    sim.step(1)
    physics.set_angular_velocity(entity, vector.create(0, 2, 0))
    sim.step(1)
    local before = spin(entity)
    fly(entity, state, {}, 60)
    assert_near(spin(entity), before, 0.2)
end)

test("a commanded rotation follows the rate target within the torque limit", function()
    local entity, state = ship({ assist = flight.RATE, max_rate = 1, torque_x = 400, torque_y = 400, torque_z = 400, response = 0.2 })
    fly(entity, state, { yaw = 1 }, 180)
    local w = scene.get64(entity, "Velocity", "Angular")
    assert_near(w.y, -1, 0.05)
    assert_near(w.x, 0, 0.02)
end)

test("attitude hold returns the ship to the pose it settled in", function()
    local entity, state = ship({ assist = flight.HOLD, torque_x = 400, torque_y = 400, torque_z = 400, response = 0.2 })
    fly(entity, state, {}, 5)
    local home = scene.get64(entity, "Transform", "Rotation")
    physics.set_angular_velocity(entity, vector.create(0.3, 0.4, -0.2))
    fly(entity, state, {}, 600)
    local now = scene.get64(entity, "Transform", "Rotation")
    assert(math.abs(dquat.dot(now, home)) > 0.9999, "dot " .. dquat.dot(now, home))
    assert(spin(entity) < 0.01)
end)

test("velocity hold converges to the held velocity and brakes after thrust", function()
    local entity, state = ship({ assist = flight.HOLD, thrust_x = 2000, thrust_y = 2000, thrust_z = 2000, response = 0.3 })
    fly(entity, state, { z = 1 }, 60)
    local held = scene.get64(entity, "Velocity", "Linear")
    assert(dvec3.length(held) > 5)
    physics.add_impulse(entity, vector.create(300, 0, 0))
    fly(entity, state, {}, 240)
    assert_near(scene.get64(entity, "Velocity", "Linear"), held, 0.05)
end)

test("velocity hold is a cruise control that releases on thrust", function()
    local entity, state = ship({ assist = flight.HOLD, thrust_z = 2000, response = 0.3 })
    fly(entity, state, { z = 1 }, 30)
    local first = speed(entity)
    fly(entity, state, {}, 120)
    assert_near(speed(entity), first, 0.1)
    fly(entity, state, { z = 1 }, 30)
    assert(speed(entity) > first + 5)
end)

test("retro brake turns the nose against the velocity and stops the ship", function()
    local entity, state = ship({
        assist = flight.RETRO,
        thrust_z = 2000,
        torque_x = 400,
        torque_y = 400,
        torque_z = 400,
        response = 0.2,
        retro_stop = 0.2,
    })
    sim.step(1)
    physics.set_velocity(entity, vector.create(20, 0, 0))
    sim.step(1)
    fly(entity, state, {}, 600)
    assert(speed(entity) < 0.3, "speed " .. speed(entity))
    assert(spin(entity) < 0.05)
end)

test("the integrated pose is exact in double precision far from the origin", function()
    local entity, state = ship({ thrust_z = 500, assist = flight.OFF })
    scene.set64(entity, "Transform", "Position", dvec3.new(1e9, 0, 0))
    sim.step(1)
    fly(entity, state, { z = 1 }, 30)
    assert_near(scene.get64(entity, "Velocity", "Linear").z, -2.5, 1e-3)
end)

test("flight state is plain data and unknown keys are rejected", function()
    local state = flight.new({ assist = flight.HOLD })
    flight.step(state, body({ velocity = dvec3.new(1, 2, 3) }), { x = 1 }, DT)
    flight.step(state, body({ velocity = dvec3.new(1, 2, 3) }), {}, DT)
    assert(plain.check(state, "flight"))
    expect_error(function() flight.configure(state, { thrust = 1 }) end, "unknown key")
end)

test("collider inertia matches the closed forms", function()
    local ball = sim.spawn("Ball", { Collider = { Shape = 1, Radius = 2 } })
    local x, y, z = flight.collider_inertia(ball, 10)
    assert_near(x, 0.4 * 10 * 4, 1e-9)
    assert_eq(x, y)
    assert_eq(y, z)
    local crate = sim.spawn("Crate", { Collider = { Shape = 0, HalfExtents = vector.create(1, 2, 3) } })
    x, y, z = flight.collider_inertia(crate, 12)
    assert_near(x, 12 / 3 * (4 + 9), 1e-9)
    assert_near(y, 12 / 3 * (1 + 9), 1e-9)
    assert_near(z, 12 / 3 * (1 + 4), 1e-9)
end)

test("PhysX inertia of a sphere is 0.4 m r squared", function()
    local entity = sim.spawn("Ball", {
        RigidBody = { Type = 2, Mass = 10, EnableGravity = false },
        Collider = { Shape = 1, Radius = 2 },
    })
    sim.set_velocity(entity, vector.create(0, 0, 0))
    sim.step(1)
    physics.add_torque(entity, vector.create(0, 16, 0))
    sim.step(1)
    assert_near(scene.get(entity, "Velocity", "Angular").y, DT, 1e-4)
end)

test("a throttle setting scales the forward thrust and adds to the z axis", function()
    local state = flight.new({ thrust_z = 300, assist = flight.OFF })
    assert_near(flight.step(state, body(), { throttle = 0.5 }, DT).z, -150, 1e-9)
    assert_near(flight.step(state, body(), { throttle = -0.25 }, DT).z, 75, 1e-9)
    assert_near(flight.step(state, body(), { throttle = 0.5, z = 1 }, DT).z, -300, 1e-9)
    local _, _, info = flight.step(state, body(), { throttle = 0.4 }, DT)
    assert_near(info.throttle, 0.4, 1e-12)
end)

test("a held throttle keeps accelerating a ship with no other input", function()
    local entity, state = ship({ thrust_z = 100, assist = flight.OFF })
    fly(entity, state, { throttle = 0.5 }, 60)
    assert_near(speed(entity), 50 / 100, 1e-6)
end)

test("the speed cap stops thrust from raising the speed past it", function()
    local entity, state = ship({ thrust_z = 1000, assist = flight.OFF, max_speed = 5 })
    fly(entity, state, { z = 1 }, 120)
    assert_near(speed(entity), 5, 1e-6)
    fly(entity, state, { z = 1 }, 60)
    assert(speed(entity) < 5 + 1e-6)
end)

test("the speed cap does not brake a ship that is already faster", function()
    local entity, state = ship({ thrust_z = 1000, assist = flight.OFF, max_speed = 5 })
    physics.set_velocity(entity, vector.create(0, 0, -20))
    sim.step(1)
    fly(entity, state, { z = 1 }, 30)
    assert_near(speed(entity), 20, 1e-6)
    fly(entity, state, { z = -1 }, 5)
    assert(speed(entity) < 20)
end)

test("speed hold converges to the target from below and above", function()
    local entity, state = ship({ thrust_z = 1000, thrust_x = 1000, thrust_y = 1000, assist = flight.OFF, response = 0.3, hold_speed = 30 })
    physics.set_velocity(entity, vector.create(0, 0, -10))
    sim.step(1)
    fly(entity, state, {}, 240)
    assert_near(speed(entity), 30, 0.05)
    physics.set_velocity(entity, vector.create(0, 0, -60))
    sim.step(1)
    fly(entity, state, {}, 480)
    assert_near(speed(entity), 30, 0.05)
end)

test("speed hold works from rest along the nose and yields to manual thrust", function()
    local entity, state = ship({ thrust_z = 1000, assist = flight.OFF, response = 0.3, hold_speed = 8 })
    fly(entity, state, {}, 300)
    assert_near(speed(entity), 8, 0.05)
    assert_near(scene.get64(entity, "Velocity", "Linear"), dvec3.new(0, 0, -8), 0.05)
    fly(entity, state, { z = 1 }, 30)
    assert(speed(entity) > 8.5)
end)

test("kill velocity stops the ship whatever way the nose points", function()
    local entity, state = ship({ thrust_x = 400, thrust_y = 400, thrust_z = 400, assist = flight.OFF, response = 0.2 })
    physics.set_velocity(entity, vector.create(3, 2, 4))
    sim.step(1)
    fly(entity, state, { kill = true }, 240)
    assert(speed(entity) < 0.01)
    fly(entity, state, {}, 30)
    assert(speed(entity) < 0.01)
end)

test("kill velocity is limited by the thrusters", function()
    local entity, state = ship({ thrust_x = 10, thrust_y = 10, thrust_z = 10, assist = flight.OFF, response = 0.05 })
    physics.set_velocity(entity, vector.create(0, 0, -100))
    sim.step(1)
    fly(entity, state, { kill = true }, 60)
    assert_near(speed(entity), 100 - 10 / 100, 1e-3)
end)

test("a held speed is the throttle-free cruise and flight state stays plain data", function()
    local entity, state = ship({ thrust_z = 1000, assist = flight.OFF, hold_speed = 4 })
    fly(entity, state, {}, 30)
    local ok, offender = plain.check(state, "flight")
    assert(ok, offender)
end)
