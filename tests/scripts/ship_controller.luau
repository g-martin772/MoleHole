local function spawn_ship(props, extra)
    local components = {
        RigidBody = { Type = 2, Mass = 100, EnableGravity = false },
        Collider = { Shape = 1, Radius = 1 },
    }
    for key, value in extra or {} do
        components[key] = value
    end
    local ship = sim.spawn("Ship", components)
    sim.set_velocity(ship, vector.create(0, 0, 0))
    local all = { ThrustX = 500, ThrustY = 500, ThrustZ = 500, TorquePitch = 400, TorqueYaw = 400, TorqueRoll = 400, ResponseTime = 0.2 }
    for key, value in props or {} do
        all[key] = value
    end
    sim.attach(ship, "ship_controller", all)
    return ship
end

local function velocity(ship) return scene.get64(ship, "Velocity", "Linear") end
local function spin(ship) return scene.get64(ship, "Velocity", "Angular") end

test("move axes thrust along the ship's right, up and forward", function()
    local ship = spawn_ship()
    sim.input("move_x")
    sim.input("move_y", -1)
    sim.input("move_z")
    sim.step(60)
    assert_near(velocity(ship), dvec3.new(5, -5, -5), 1e-2)
end)

test("without input the ship coasts", function()
    local ship = spawn_ship({ Assist = 0 })
    sim.step(1)
    physics.set_velocity(ship, vector.create(3, 0, 0))
    sim.step(60)
    assert_near(velocity(ship), dvec3.new(3, 0, 0), 1e-4)
end)

test("keyboard and mouse look turn the nose", function()
    local ship = spawn_ship({ Assist = 0 })
    sim.input("yaw")
    sim.step(10)
    assert(spin(ship).y < -0.1)
    sim.input("yaw", false)
    sim.input("pitch")
    sim.step(10)
    assert(spin(ship).x > 0.1)
    sim.input("pitch", false)
    sim.input("roll")
    sim.step(10)
    assert(spin(ship).z < -0.1)
end)

test("mouse up pitches up and invert flips it", function()
    local ship = spawn_ship({ Assist = 0 })
    for _ = 1, 10 do
        sim.input_mouse(0, -10)
        sim.step(1)
    end
    assert(spin(ship).x > 0.1)
    local other = spawn_ship({ Assist = 0, InvertPitch = true })
    for _ = 1, 10 do
        sim.input_mouse(0, -10)
        sim.step(1)
    end
    assert(spin(other).x < spin(ship).x)
end)

test("the assist key cycles off, rate, hold, retro", function()
    spawn_ship({ Assist = 1 })
    sim.step(1)
    for _ = 1, 4 do
        sim.input_tap("assist")
        sim.step(1)
    end
    local seen = {}
    for _, event in sim.events("assist_mode") do
        table.insert(seen, event.args[1])
    end
    assert_eq(seen, { 2, 3, 0, 1 })
end)

test("rate assist stops the spin that off leaves", function()
    local off = spawn_ship({ Assist = 0 })
    local damped = spawn_ship({ Assist = 1 })
    sim.step(1)
    physics.set_angular_velocity(off, vector.create(0, 1, 0))
    physics.set_angular_velocity(damped, vector.create(0, 1, 0))
    sim.step(120)
    assert(spin(off).y > 0.9)
    assert(math.abs(spin(damped).y) < 0.05)
end)

test("the G limit and mass override are properties", function()
    local limited = spawn_ship({ GLimit = 0.1 })
    local light = spawn_ship({ MassOverride = 25 })
    sim.input("move_z")
    sim.step(60)
    assert_near(dvec3.length(velocity(limited)), 0.1 * 9.80665, 1e-3)
    assert_near(dvec3.length(velocity(light)), 20, 1e-2)
end)

test("holding boost multiplies the thrust", function()
    local ship = spawn_ship({ Boost = 3 })
    sim.input("move_z")
    sim.input("boost")
    sim.step(60)
    assert_near(dvec3.length(velocity(ship)), 15, 1e-2)
end)

test("thrust is real newtons whatever the time scale", function()
    time.set_scale(0.01)
    local ship = spawn_ship({ ThrustZ = 5e4 })
    sim.input("move_z")
    sim.step(60)
    assert_near(dvec3.length(velocity(ship)), 5e4 / 100 * 0.01, 1e-3)
end)

test("a free-fly camera hands control away from the ship and back", function()
    local ship = spawn_ship()
    sim.input("move_z")
    sim.step(30)
    local gained = dvec3.length(velocity(ship))
    sim.emit("camera_mode", "fly")
    sim.step(30)
    assert_near(dvec3.length(velocity(ship)), gained, 0.1)
    sim.emit("camera_mode", "chase")
    sim.step(30)
    assert(dvec3.length(velocity(ship)) > gained + 2)
end)

test("a ship that is not dynamic is left alone and does not error", function()
    local ship = spawn_ship(nil, { RigidBody = { Type = 0, Mass = 100 } })
    sim.input("move_z")
    sim.step(5)
    assert_near(scene.get64(ship, "Transform", "Position"), dvec3.new(0, 0, 0), 1e-9)
end)

test("far from the origin the thrust is exact", function()
    local ship = spawn_ship()
    scene.set64(ship, "Transform", "Position", dvec3.new(7.4e5, 0, 0))
    sim.input("move_z")
    sim.step(60)
    assert_near(dvec3.length(velocity(ship)), 5, 1e-2)
end)
