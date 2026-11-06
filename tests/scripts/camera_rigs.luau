local rigs = require("lib/camera_rigs")
local dquat_lib = require("lib/dquat")

local function spawn_ship(position, rotation)
    local ship = sim.spawn("Ship", {
        Transform = { Position = position or vector.create(0, 0, 0) },
        RigidBody = { Type = 2, Mass = 100, EnableGravity = false },
        Collider = { Shape = 1, Radius = 1 },
    })
    if rotation then
        scene.set64(ship, "Transform", "Rotation", rotation)
    end
    sim.set_velocity(ship, vector.create(0, 0, 0))
    return ship
end

local function spawn_camera(script, props, primary, fov)
    local camera = sim.spawn("Camera", { Camera = { Primary = primary ~= false, Fov = fov or 60 } })
    sim.attach(camera, script, props)
    return camera
end

local function position(entity) return scene.get64(entity, "Transform", "Position") end
local function rotation(entity) return scene.get64(entity, "Transform", "Rotation") end
local function fov(entity) return scene.get(entity, "Camera", "Fov") end

local function tilt(entity, ship)
    return dquat_lib.angle(rotation(ship), rotation(entity))
end

local function shake_trace(seed, props, ticks)
    local ship = spawn_ship()
    local settings = { Target = ship, Offset = vector.create(0, 0, 0), Seed = seed, ShakeAngle = 4, ShakeOffset = 0.5, ShakeDecay = 1 }
    for k, v in props or {} do
        settings[k] = v
    end
    local camera = spawn_camera("cockpit_camera", settings, true)
    sim.step(2)
    sim.emit("camera_shake", 1)
    local trace = {}
    for i = 1, ticks do
        sim.step(1)
        trace[i] = { tilt(camera, ship), dvec3.length(position(camera) - position(ship)) }
    end
    sim.destroy(camera)
    sim.destroy(ship)
    sim.step(2)
    return trace
end

local function same(a, b): boolean
    for i = 1, #a do
        if a[i][1] ~= b[i][1] or a[i][2] ~= b[i][2] then
            return false
        end
    end
    return true
end

test("noise is a pure function of the seed, bounded and continuous", function()
    for i = 0, 200 do
        local t = i * 0.173
        local value = rigs.noise(77, 3, t)
        assert_eq(value, rigs.noise(77, 3, t))
        assert(math.abs(value) <= 1)
        assert(math.abs(rigs.noise(77, 3, t + 1e-4) - value) < 1e-2, "jump at " .. t)
        assert(math.abs(rigs.fractal(77, 3, t)) <= 1)
    end
    local differs = false
    for i = 0, 50 do
        differs = differs or rigs.noise(1, 0, i + 0.5) ~= rigs.noise(2, 0, i + 0.5)
    end
    assert(differs)
    assert_near(rigs.noise(5, 0, 12), rigs.noise(5, 0, 12.0000001), 1e-6)
    assert(rigs.noise(5, 0, -3.5) == rigs.noise(5, 0, -3.5))
end)

test("shake pose scales with trauma squared and vanishes at zero", function()
    local q0, o0 = rigs.shake_pose(9, 0.37, 0, 3, 1)
    assert_near(dquat_lib.angle(q0, dquat_lib.IDENTITY), 0, 1e-12)
    assert_near(dvec3.length(o0), 0, 1e-12)
    local full = dquat_lib.angle(rigs.shake_pose(9, 0.37, 1, 3, 1), dquat_lib.IDENTITY)
    local half = dquat_lib.angle(rigs.shake_pose(9, 0.37, 0.5, 3, 1), dquat_lib.IDENTITY)
    assert(full > 0)
    assert_near(half, full * 0.25, full * 0.01)
    local a = rigs.shake_pose(9, 0.37, 1, 3, 1)
    local b = rigs.shake_pose(9, 0.37, 1, 3, 1)
    assert_near(dquat_lib.angle(a, b), 0, 1e-12)
end)

test("trauma decays linearly and stops at zero", function()
    assert_near(rigs.decay(1, 0.5, 1), 0.5, 1e-12)
    assert_near(rigs.decay(0.5, 0.5, 0.5), 0.25, 1e-12)
    assert_eq(rigs.decay(0.1, 1, 10), 0)
end)

test("a camera_shake event shakes the active camera, deterministically per seed, and decays to the exact base pose", function()
    local first = shake_trace(1234, nil, 90)
    local second = shake_trace(1234, nil, 90)
    assert(same(first, second), "same seed must replay identically")
    assert(not same(first, shake_trace(4321, nil, 90)), "another seed must differ")
    local peak = 0
    for i = 1, 10 do
        peak = math.max(peak, first[i][1])
    end
    assert(peak > math.rad(0.5), "shake too weak: " .. peak)
    assert(peak < math.rad(8), "shake too strong: " .. peak)
    local late = 0
    for i = 30, 50 do
        late = math.max(late, first[i][1])
    end
    assert(late < peak * 0.5, "no decay")
    for i = 70, 90 do
        assert_eq(first[i][1], 0)
        assert_eq(first[i][2], 0)
    end
end)

test("larger impulses add up to a clamp and shake harder", function()
    local small = shake_trace(5, nil, 5)
    local ship = spawn_ship()
    local camera = spawn_camera("cockpit_camera", { Target = ship, Seed = 5, ShakeAngle = 4, ShakeOffset = 0.5, ShakeDecay = 1 }, true)
    sim.step(2)
    sim.emit("camera_shake", 0.4)
    sim.emit("camera_shake", 0.4)
    sim.emit("camera_shake", 5)
    sim.step(1)
    local clamped = tilt(camera, ship)
    sim.step(1)
    local strong = tilt(camera, ship)
    assert(clamped > 0 and strong > 0)
    assert_near(clamped, small[1][1], small[1][1] * 0.2, "trauma clamps at 1")
end)

test("an inactive camera ignores shake events and the pose stays exact", function()
    local ship = spawn_ship()
    local active = spawn_camera("cockpit_camera", { Target = ship, Seed = 3 }, true)
    local idle = spawn_camera("cockpit_camera", { Target = ship, Seed = 3 }, false)
    sim.step(3)
    sim.emit("camera_shake", 1)
    sim.step(3)
    assert(scene.get(active, "Camera", "Primary"))
    assert(tilt(active, ship) > 0)
    scene.set(idle, "Camera", "Primary", true)
    sim.step(2)
    assert_eq(tilt(idle, ship), 0)
end)

test("the shake does not leak into the chase smoothing", function()
    local ship = spawn_ship()
    local camera = spawn_camera("chase_camera", { Target = ship, Distance = 30, Height = 8, LookAhead = 40, SmoothTime = 0.1, TurnTime = 0.05, Seed = 7, ShakeOffset = 1, ShakeDecay = 2 }, true)
    sim.step(60)
    sim.emit("camera_shake", 1)
    sim.step(4)
    local shaken = position(camera) - position(ship)
    assert(dvec3.distance(shaken, dvec3.new(0, 8, 30)) > 0.01)
    sim.step(120)
    assert_near(position(camera), dvec3.new(0, 8, 30), 0.05)
    local forward = dquat.rotate(rotation(camera), dvec3.new(0, 0, -1))
    assert_near(forward, dvec3.normalize(dvec3.new(0, 0, -40) - dvec3.new(0, 8, 30)), 1e-3)
end)

test("handheld noise is seeded, bounded and off by default", function()
    local ship = spawn_ship()
    local calm = spawn_camera("cockpit_camera", { Target = ship }, true)
    sim.step(30)
    assert_eq(tilt(calm, ship), 0)
    sim.destroy(calm)
    sim.step(1)

    local function run(seed)
        local camera = spawn_camera("cockpit_camera", { Target = ship, Handheld = 1, Seed = seed }, true)
        local values, most = {}, 0
        for i = 1, 120 do
            sim.step(1)
            values[i] = tilt(camera, ship)
            most = math.max(most, values[i])
        end
        sim.destroy(camera)
        sim.step(1)
        return values, most
    end
    local a, most = run(11)
    local b = run(11)
    local c = run(12)
    local equal, other = true, false
    for i = 1, 120 do
        equal = equal and a[i] == b[i]
        other = other or a[i] ~= c[i]
    end
    assert(equal and other)
    assert(most > math.rad(0.05) and most < math.rad(2.5), "handheld amplitude " .. math.deg(most))
end)

local TRACK = { dvec3.new(0, 0, 0), dvec3.new(100, 0, 0), dvec3.new(200, 50, 0), dvec3.new(300, 50, 100) }

test("a rail starts and ends exactly on its first and last point and clamps beyond", function()
    local rail = rigs.rail(TRACK)
    assert_near(rigs.rail_at(rail, 0), TRACK[1], 1e-9)
    assert_near(rigs.rail_at(rail, rail.length), TRACK[4], 1e-6)
    assert_near(rigs.rail_at(rail, -50), TRACK[1], 1e-9)
    assert_near(rigs.rail_at(rail, rail.length + 50), TRACK[4], 1e-6)
    assert(rail.length > 300 and rail.length < 450)
    local _, tangent = rigs.rail_at(rail, 0)
    assert_near(tangent, dvec3.new(1, 0, 0), 1e-9)
    local previous = rigs.rail_at(rail, 0)
    for i = 1, 50 do
        local now = rigs.rail_at(rail, rail.length * i / 50)
        assert_near(dvec3.distance(now, previous), rail.length / 50, rail.length / 50 * 0.05)
        previous = now
    end
end)

test("a closed rail wraps and a far rail keeps double precision", function()
    local rail = rigs.rail({ dvec3.new(0, 0, 0), dvec3.new(10, 0, 0), dvec3.new(10, 10, 0), dvec3.new(0, 10, 0) }, true)
    assert_near(rigs.rail_at(rail, 0), rigs.rail_at(rail, rail.length), 1e-6)
    assert_near(rigs.rail_at(rail, 3), rigs.rail_at(rail, 3 + rail.length * 2), 1e-6)
    local origin = dvec3.new(4e12, -2e12, 7e11)
    local far = rigs.rail({ origin, origin + dvec3.new(10, 0, 0), origin + dvec3.new(20, 5, 0) })
    assert_near(rigs.rail_at(far, 0), origin, 1e-3)
    assert_near(rigs.rail_at(far, far.length), origin + dvec3.new(20, 5, 0), 1e-3)
end)

test("the nearest point on a rail is the projection of the subject", function()
    local rail = rigs.rail({ dvec3.new(0, 0, 0), dvec3.new(100, 0, 0), dvec3.new(200, 0, 0) })
    local s, distance = rigs.rail_nearest(rail, dvec3.new(60, 25, 0))
    assert_near(s, 60, 0.05)
    assert_near(distance, 25, 0.05)
    s, distance = rigs.rail_nearest(rail, dvec3.new(-30, 0, 0))
    assert_near(s, 0, 0.05)
    assert_near(distance, 30, 0.05)
    s = rigs.rail_nearest(rail, dvec3.new(500, 10, 0))
    assert_near(s, rail.length, 0.05)
end)

test("parse points reads metres and rejects malformed lists", function()
    local points = rigs.parse_points("0 0 0; 1e12, 5 6 ;7 8 9")
    assert_eq(#points, 3)
    assert_near(points[2], dvec3.new(1e12, 5, 6), 1e-3)
    expect_error(function() rigs.parse_points("1 2") end, "bad point")
    expect_error(function() rigs.rail({ dvec3.new(0, 0, 0) }) end, "at least 2")
end)

test("the rail camera slides along the rail at its speed and parks on the last point", function()
    local camera = spawn_camera("rail_camera", { Points = "0 0 0; 100 0 0; 200 50 0", Follow = false, Speed = 50 }, true)
    sim.step(1)
    assert_near(position(camera), dvec3.new(0, 0, 0), 1)
    sim.step(60)
    local rail = rigs.rail({ dvec3.new(0, 0, 0), dvec3.new(100, 0, 0), dvec3.new(200, 50, 0) })
    assert_near(position(camera), rigs.rail_at(rail, 50 * 61 * sim.dt()), 0.01)
    sim.step(600)
    assert_near(position(camera), dvec3.new(200, 50, 0), 1e-6)
    local forward = dquat.rotate(rotation(camera), dvec3.new(0, 0, -1))
    assert(forward.x > 0.85)
end)

test("the rail camera keeps the nearest rail point to its subject and looks at it", function()
    local ship = spawn_ship(vector.create(40, 30, 0))
    local camera = spawn_camera("rail_camera", { Points = "0 0 0; 100 0 0; 200 0 0", Target = ship, SmoothTime = 0.05 }, true)
    sim.step(2)
    assert_near(position(camera), dvec3.new(40, 0, 0), 0.05)
    local forward = dquat.rotate(rotation(camera), dvec3.new(0, 0, -1))
    assert_near(forward, dvec3.new(0, 1, 0), 1e-3)
    scene.set64(ship, "Transform", "Position", dvec3.new(150, 20, 10))
    sim.step(120)
    assert_near(position(camera), dvec3.new(150, 0, 0), 0.05)
    scene.set64(ship, "Transform", "Position", dvec3.new(900, 20, 10))
    sim.step(120)
    assert_near(position(camera), dvec3.new(200, 0, 0), 0.05)
end)

test("dolly zoom keeps the subject width constant analytically", function()
    local base_fov, base_distance = 40, 120
    local width = rigs.subject_width(base_fov, base_distance)
    for _, distance in { 20, 35.5, 80, 120, 200, 400 } do
        local field = rigs.dolly_fov(base_fov, base_distance, distance)
        assert_near(rigs.subject_width(field, distance), width, width * 1e-12, "distance " .. distance)
        assert_near(rigs.dolly_distance(base_fov, base_distance, field), distance, distance * 1e-9)
    end
    assert(rigs.dolly_fov(base_fov, base_distance, 1e-6) == rigs.FOV_MAX)
    assert(rigs.dolly_fov(base_fov, base_distance, 1e12) == rigs.FOV_MIN)
end)

test("the dolly zoom camera moves in and widens the lens at constant subject size", function()
    local ship = spawn_ship()
    local camera = spawn_camera("dolly_zoom_camera", { Target = ship, Distance = 100, EndDistance = 40, Duration = 2, Ease = "linear" }, true, 30)
    sim.step(1)
    local width = rigs.subject_width(fov(camera), dvec3.distance(position(camera), position(ship)))
    assert_near(dvec3.distance(position(camera), position(ship)), 100, 1)
    for i = 1, 6 do
        sim.step(20)
        local d = dvec3.distance(position(camera), position(ship))
        assert_near(rigs.subject_width(fov(camera), d), width, width * 1e-4, "step " .. i)
    end
    sim.step(60)
    assert_near(dvec3.distance(position(camera), position(ship)), 40, 1e-6)
    assert_near(fov(camera), rigs.dolly_fov(30, 100, 40), 1e-4)
    assert(fov(camera) > 30)
    local forward = dquat.rotate(rotation(camera), dvec3.new(0, 0, -1))
    assert_near(forward, dvec3.new(0, 0, -1), 1e-6)
end)

test("ride-along keeps its offset in the ship's frame while the ship turns", function()
    local ship = spawn_ship(vector.create(10, 0, 0))
    local camera = spawn_camera("ride_along_camera", { Target = ship, Offset = vector.create(3, 1, 5), Yaw = 90 }, true)
    sim.step(2)
    assert_near(position(camera) - position(ship), dvec3.new(3, 1, 5), 1e-6)
    for _, angle in { 0.4, 1.9, -2.6 } do
        local q = dquat.from_axis_angle(dvec3.normalize(dvec3.new(0.3, 1, -0.2)), angle)
        scene.set64(ship, "Transform", "Rotation", q)
        sim.step(1)
        assert_near(position(camera) - position(ship), q * dvec3.new(3, 1, 5), 1e-6)
        local expected = dquat.mul(q, dquat.from_euler(0, 90, 0))
        assert_near(dquat_lib.angle(rotation(camera), expected), 0, 1e-6)
    end
end)

test("ride-along can look at a focus entity with lead", function()
    local ship = spawn_ship()
    local focus = spawn_ship(vector.create(100, 0, -50))
    local camera = spawn_camera("ride_along_camera", { Target = ship, Focus = focus, Offset = vector.create(0, 5, 0), LeadTime = 3 }, true)
    sim.step(2)
    local forward = dquat.rotate(rotation(camera), dvec3.new(0, 0, -1))
    assert_near(forward, dvec3.normalize(position(focus) - position(camera)), 1e-6)
    local pure = rigs.look_at_lead(dvec3.new(0, 0, 0), dvec3.new(100, 0, 0), dvec3.new(0, 10, 0), 5)
    assert_near(dquat.rotate(pure, dvec3.new(0, 0, -1)), dvec3.normalize(dvec3.new(100, 50, 0)), 1e-9)
end)

test("lens keys animate the field of view on the sim clock and hold the last key", function()
    local ship = spawn_ship()
    local camera = spawn_camera("ride_along_camera", { Target = ship, LensKeys = "fov 0 60; fov 2 30" }, true)
    sim.step(60)
    local clock = 60 * sim.dt()
    assert_near(fov(camera), 60 - 15 * clock, 1e-3)
    sim.step(300)
    assert_near(fov(camera), 30, 1e-4)
    local tracks = rigs.lens_tracks("fov 0 60 smoothstep; fov 4 30; aperture 0 2.8; aperture 1 8; exposure 0 -1; exposure 2 1")
    assert(tracks.fov and tracks.aperture and tracks.exposure)
    assert_near(require("lib/curves").track_eval(tracks.aperture, 0.5), 5.4, 1e-9)
    assert_near(require("lib/curves").track_eval(tracks.exposure, 1), 0, 1e-9)
    expect_error(function() rigs.lens_tracks("zoom 0 1") end, "bad lens key")
end)

test("impact_shake turns a contact impulse into a camera_shake event", function()
    local upper = sim.spawn("Upper", {
        Transform = { Position = vector.create(0, 3, 0) },
        RigidBody = { Type = 2, Mass = 1, EnableGravity = false },
        Collider = { Shape = 1, Radius = 0.5 },
    })
    local lower = sim.spawn("Lower", {
        Transform = { Position = vector.create(0, 0, 0) },
        RigidBody = { Type = 2, Mass = 1, EnableGravity = false },
        Collider = { Shape = 1, Radius = 0.5 },
    })
    sim.set_velocity(upper, vector.create(0, -4, 0))
    sim.set_velocity(lower, vector.create(0, -1, 0))
    sim.attach(upper, "impact_shake", { FullImpulse = 4, MinImpulse = 0.5 })
    local camera = spawn_camera("cockpit_camera", { Target = upper, Seed = 2 }, true)
    sim.step(120)
    local shakes = sim.events("camera_shake")
    assert_eq(#shakes, 1)
    local amount = shakes[1].args[1]
    assert(amount > 0.3 and amount <= 1, "amount " .. amount)
    assert(scene.get(camera, "Camera", "Primary"))
end)

test("camera_cycle releases the shake offset when it hands over to the next camera", function()
    local ship = spawn_ship()
    local b = spawn_camera("cockpit_camera", { Target = ship }, false)
    local a = spawn_camera("cockpit_camera", { Target = ship, Seed = 8, Next = b }, true)
    sim.step(3)
    sim.emit("camera_shake", 1)
    sim.step(2)
    assert(tilt(a, ship) > 0)
    sim.input_tap("camera")
    sim.step(2)
    assert_near(tilt(a, ship), 0, 1e-6)
end)

test("orbit pose sits on a sphere around the anchor and looks back at it", function()
    local anchor = dvec3.new(5e11, 0, -3e11)
    for _, case in { { 0, 0 }, { 90, 0 }, { 30, 45 }, { -120, -20 } } do
        local position, view = rigs.orbit_pose(anchor, case[1], case[2], 50, 0)
        assert_near(dvec3.distance(position, anchor), 50, 1e-3)
        assert_near(dquat.rotate(view, dvec3.new(0, 0, -1)), dvec3.normalize(anchor - position), 1e-5)
    end
end)
