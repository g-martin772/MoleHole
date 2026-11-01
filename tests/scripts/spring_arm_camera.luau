local orbit = require("lib/orbit")

local function rig(props)
    local target = sim.spawn("Target", { Transform = { Position = vector.create(100, 50, -20) } })
    local camera = sim.spawn("Camera")
    props.Target = target
    sim.attach(camera, "spring_arm_camera", props)
    return camera, target
end

local function goal(target, yaw, pitch, arm)
    local dx, dy, dz = orbit.direction(yaw, pitch)
    return scene.get(target, "Transform", "Position") + vector.create(dx, dy, dz) * arm
end

local function distance(a, b)
    local d = a - b
    return math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z)
end

test("without smoothing the camera sits on the arm and looks at the target", function()
    local camera, target = rig({ ArmLength = 1000, Yaw = 0, Pitch = 30, OrbitSpeed = 0, Smoothing = 0 })
    sim.step(1)
    local position = scene.get(camera, "Transform", "Position")
    assert_near(position, goal(target, 0, 30, 1000), 1e-2)
    local from = position - scene.get(target, "Transform", "Position")
    assert_near(scene.get(camera, "Transform", "Rotation"), scene.look_at(from, vector.zero), 1e-3)
end)

test("the yaw advances with the sim clock", function()
    local camera, target = rig({ ArmLength = 1000, Yaw = 0, Pitch = 10, OrbitSpeed = 90, Smoothing = 0 })
    sim.step(60)
    assert_near(time.elapsed(), 1, 1e-9)
    assert_near(scene.get(camera, "Transform", "Position"), goal(target, 90, 10, 1000), 0.1)
end)

test("smoothing closes the gap exponentially with a half-life of Smoothing seconds", function()
    local camera, target = rig({ ArmLength = 1000, Pitch = 0, OrbitSpeed = 0, Smoothing = 0.5 })
    local aim = goal(target, 0, 0, 1000)
    local gap = distance(scene.get(camera, "Transform", "Position"), aim)
    sim.step(60)
    assert_rel(distance(scene.get(camera, "Transform", "Position"), aim), gap * 2 ^ -2, 1e-3)
end)

test("a missing or destroyed target leaves the camera where it is", function()
    local camera = sim.spawn("Camera", { Transform = { Position = vector.create(5, 6, 7) } })
    sim.attach(camera, "spring_arm_camera")
    sim.step(3)
    assert_eq(scene.get(camera, "Transform", "Position"), vector.create(5, 6, 7))

    local cam2, target = rig({ Smoothing = 0, OrbitSpeed = 0 })
    sim.step(1)
    local at = scene.get(cam2, "Transform", "Position")
    sim.destroy(target)
    sim.step(5)
    assert_eq(scene.get(cam2, "Transform", "Position"), at)
end)
