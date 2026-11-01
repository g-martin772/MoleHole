local hud = require("lib/hud")

local function recorder()
    local calls = {}
    local h = {}
    for _, name in { "text", "line", "rect", "circle", "arc" } do
        h[name] = function(...)
            table.insert(calls, { name = name, ... })
        end
    end
    return h, calls
end

local function count(calls, name)
    local n = 0
    for _, call in calls do
        if call.name == name then
            n += 1
        end
    end
    return n
end

local function texts(commands)
    local list = {}
    for _, command in commands do
        if command.kind == "text" then
            table.insert(list, command.text)
        end
    end
    return list
end

local function any_text(commands, needle)
    for _, text in texts(commands) do
        if string.find(text, needle, 1, true) then
            return true
        end
    end
    return false
end

local function spawn_ship(velocity, rotation, props)
    local ship = sim.spawn("Ship", {
        RigidBody = { Type = 2, Mass = 100, EnableGravity = false },
        Collider = { Shape = 1, Radius = 1 },
    })
    sim.set_velocity(ship, velocity or vector.create(0, 0, 0))
    if rotation then
        scene.set64(ship, "Transform", "Rotation", rotation)
    end
    sim.attach(ship, "telemetry_hud", props)
    return ship
end

local function camera_at(position)
    local camera = sim.spawn("Camera", { Camera = { Primary = true, Fov = 60 } })
    scene.set64(camera, "Transform", "Position", position)
    return camera
end

test("speed formats in m/s, km/s and Mm/s with the c fraction", function()
    assert_eq(hud.format_speed(12.34), "12.3 m/s")
    assert_eq(hud.format_speed(-2500), "2.50 km/s")
    assert_eq(hud.format_speed(1.34e7), "13.40 Mm/s")
    assert_eq(hud.format_beta(hud.C * 0.5), "0.5000 c")
    assert_eq(hud.format_beta(hud.C * 0.999), "0.999000 c")
    assert_eq(hud.format_distance(950), "950.0 m")
    assert_eq(hud.format_distance(7.4e5), "740.00 km")
    assert_eq(hud.format_distance(3e12), "20.054 AU")
end)

test("gamma follows the Lorentz factor and stays finite at c", function()
    assert_near(hud.gamma(0), 1, 1e-12)
    assert_near(hud.gamma(hud.C * 0.6), 1.25, 1e-9)
    assert(hud.gamma(hud.C * 2) < 1e9)
end)

test("the text gauge fills in proportion and clamps", function()
    assert_eq(hud.text_gauge(0.5, 0, 1, 10), "[#####-----]")
    assert_eq(hud.text_gauge(2, 0, 1, 4), "[####]")
    assert_eq(hud.text_gauge(-3, 0, 1, 4), "[----]")
end)

test("attitude gives heading, pitch and bank against the world axes", function()
    local level = hud.attitude(dquat.new(0, 0, 0, 1))
    assert_near(level.heading, 0, 1e-9)
    assert_near(level.pitch, 0, 1e-9)
    assert_near(level.roll, 0, 1e-9)
    local east = hud.attitude(dquat.from_axis_angle(dvec3.new(0, 1, 0), -math.pi / 2))
    assert_near(east.heading, 90, 1e-9)
    local up = hud.attitude(dquat.from_axis_angle(dvec3.new(1, 0, 0), math.rad(30)))
    assert_near(up.pitch, 30, 1e-9)
    local banked = hud.attitude(dquat.from_axis_angle(dvec3.new(0, 0, 1), math.rad(-20)))
    assert_near(banked.roll, 20, 1e-9)
end)

test("a world direction projects to the screen centre ahead and to the edge behind", function()
    local camera = dquat.new(0, 0, 0, 1)
    local point, _, front = hud.project(dvec3.new(0, 0, -1), camera, 60)
    assert(front)
    assert_near(point.x, 0, 1e-12)
    assert_near(point.y, 0.5, 1e-12)
    local up = hud.project(dvec3.normalize(dvec3.new(0, math.tan(math.rad(30)), -1)), camera, 60)
    assert_near(up.y, 0, 1e-9)
    local right = hud.project(dvec3.normalize(dvec3.new(math.tan(math.rad(30)), 0, -1)), camera, 60)
    assert_near(right.x, 0.5, 1e-9)
    local none, angle, behind = hud.project(dvec3.new(1, 0, 1), camera, 60)
    assert(none == nil and not behind)
    assert_near(angle, math.pi / 2, 1e-9)
end)

test("nearest body picks the closest hole or sphere and reports altitude", function()
    local hole = sim.spawn("Hole", { BlackHole = { Mass = 1.9884e30 } })
    scene.set64(hole, "Transform", "Position", dvec3.new(0, 0, 0))
    local planet = sim.spawn("Planet", { Sphere = { Radius = 1000 } })
    scene.set64(planet, "Transform", "Position", dvec3.new(50000, 0, 0))
    local near = hud.nearest_body(dvec3.new(52500, 0, 0))
    assert_eq(near.name, "Planet")
    assert_near(near.distance, 2500, 1e-9)
    assert_near(near.altitude, 1500, 1e-9)
    local far = hud.nearest_body(dvec3.new(1000, 0, 0))
    assert_eq(far.name, "Hole")
    assert_near(far.in_rg, 1000 / 1476.625, 1e-3)
    assert(far.altitude < far.distance)
end)

test("bars, markers and the attitude ball draw through the given surface", function()
    local h, calls = recorder()
    hud.bar(h, 0.1, 0.1, 0.2, 0.02, 0.5)
    assert_eq(count(calls, "rect"), 3)
    h, calls = recorder()
    hud.vbar(h, 0.1, 0.1, 0.02, 0.3, 0.5, -1, 1)
    assert_eq(count(calls, "line"), 1)
    h, calls = recorder()
    hud.marker(h, "prograde", 0.5, 0.5, 0.02)
    assert_eq(count(calls, "circle"), 2)
    h, calls = recorder()
    hud.marker(h, "retrograde", 0.5, 0.5, 0.02)
    assert_eq(count(calls, "line"), 2)
    h, calls = recorder()
    hud.attitude_ball(h, 0, 0.8, 0.07, hud.attitude(dquat.from_axis_angle(dvec3.new(1, 0, 0), 0.2)), dvec3.new(0, 0, -5))
    assert(count(calls, "line") > 10)
    assert(count(calls, "circle") >= 3)
end)

test("the colour fade keeps the rgb and sets the alpha byte", function()
    assert_eq(hud.fade("#ff8000", 0.5), "#ff800080")
    assert_eq(hud.fade("#ff8000", 2), "#ff8000ff")
end)

test("the telemetry HUD emits absolute speed, gamma and the attitude readout", function()
    camera_at(dvec3.new(0, 0, 100))
    spawn_ship(vector.create(0, 0, -1.5e8))
    sim.step(3)
    local commands = sim.hud()
    assert(#commands > 40)
    assert(any_text(commands, "SPEED"))
    assert(any_text(commands, "150.00 Mm/s"))
    assert(any_text(commands, "0.5003 c"))
    assert(any_text(commands, "gamma 1.1"))
    assert(any_text(commands, "HDG 000"))
    assert(any_text(commands, "PIT +0.0"))
end)

test("the HUD shows relative speed to a reference entity", function()
    camera_at(dvec3.new(0, 0, 100))
    local station = sim.spawn("Station", { RigidBody = { Type = 2, Mass = 100, EnableGravity = false }, Collider = { Shape = 1, Radius = 1 } })
    sim.set_velocity(station, vector.create(0, 0, -600))
    local ship = sim.spawn("Ship", { RigidBody = { Type = 2, Mass = 100, EnableGravity = false }, Collider = { Shape = 1, Radius = 1 } })
    sim.set_velocity(ship, vector.create(0, 0, -1000))
    sim.attach(ship, "telemetry_hud", { Reference = station })
    sim.step(2)
    local commands = sim.hud()
    assert(any_text(commands, "REL Station  400.0 m/s"))
    assert(any_text(commands, "1.00 km/s"))
end)

test("throttle, assist, boost and camera mode appear from the controller", function()
    camera_at(dvec3.new(0, 0, 100))
    local ship = spawn_ship(nil, nil, {})
    sim.attach(ship, "ship_controller", { ThrustZ = 500, Assist = 2, ThrottleRate = 10 })
    sim.emit("camera_mode", "chase")
    sim.input("throttle")
    sim.step(8)
    sim.input("throttle", false)
    sim.input("boost")
    sim.step(3)
    local commands = sim.hud()
    assert(any_text(commands, "THR 100%"))
    assert(any_text(commands, "ASSIST HOLD"))
    assert(any_text(commands, "BOOST ON"))
    assert(any_text(commands, "CAMERA CHASE"))
    assert(any_text(commands, "CONTROL mouse + keys"))
end)

test("speed hold and kill velocity are named in the readout", function()
    camera_at(dvec3.new(0, 0, 100))
    local ship = spawn_ship(vector.create(0, 0, -20))
    sim.attach(ship, "ship_controller", { ThrustZ = 5000, Assist = 0 })
    sim.step(1)
    sim.input_tap("speed_hold")
    sim.step(3)
    assert(any_text(sim.hud(), "SPEED HOLD 20.0 m/s"))
    sim.input("kill_velocity")
    sim.step(3)
    assert(any_text(sim.hud(), "KILL VELOCITY"))
end)

test("the H action cycles full, compact and off, and off draws nothing", function()
    camera_at(dvec3.new(0, 0, 100))
    spawn_ship(vector.create(0, 0, -10))
    sim.step(2)
    local full = #sim.hud()
    sim.input_tap("hud")
    sim.step(2)
    local compact = sim.hud()
    assert(#compact > 0 and #compact < full)
    assert(not any_text(compact, "ACC"))
    sim.input_tap("hud")
    sim.step(2)
    assert_eq(#sim.hud(), 0)
    sim.input_tap("hud")
    sim.step(2)
    assert_eq(#sim.hud(), full)
end)

test("the prograde marker lands on the screen centre when flying straight at the camera view", function()
    camera_at(dvec3.new(0, 0, 100))
    spawn_ship(vector.create(0, 0, -10), nil, { Markers = true, Attitude = false })
    sim.step(2)
    local circles = {}
    for _, command in sim.hud() do
        if command.kind == "circle" and command.space == "center" then
            table.insert(circles, command)
        end
    end
    assert(#circles >= 2)
    assert_near(circles[1].x, 0, 1e-6)
    assert_near(circles[1].y, 0.5, 1e-6)
end)

test("a target gets a marker, range and closing speed", function()
    camera_at(dvec3.new(0, 0, 100))
    local target = sim.spawn("Beacon")
    scene.set64(target, "Transform", "Position", dvec3.new(0, 0, -4000))
    spawn_ship(vector.create(0, 0, -100), nil, { Target = target })
    sim.step(2)
    local commands = sim.hud()
    assert(any_text(commands, "TGT Beacon  4.00 km"))
    assert(any_text(commands, "CLOSING 100.0 m/s"))
    assert(any_text(commands, "Beacon"))
end)

test("a marker behind the camera turns into an edge arrow", function()
    camera_at(dvec3.new(0, 0, 100))
    spawn_ship(vector.create(0, 0, 10), nil, { Markers = true, Attitude = false })
    sim.step(2)
    local lines = 0
    for _, command in sim.hud() do
        if command.kind == "line" and command.space == "center" then
            lines += 1
        end
    end
    assert(lines >= 2)
end)

test("the HUD never changes the simulation state", function()
    camera_at(dvec3.new(0, 0, 100))
    local with = spawn_ship(vector.create(0, 0, -10))
    sim.step(10)
    local drawn = scene.get64(with, "Transform", "Position")
    assert(#sim.hud() > 0)
    assert_near(drawn, dvec3.new(0, 0, -10 / 6), 0.05)
end)
