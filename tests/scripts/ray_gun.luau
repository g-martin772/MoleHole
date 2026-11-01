test("ray gun pushes the body it hits and reports the hit", function()
    local gun = sim.spawn("Gun")
    local target = sim.spawn("Target", {
        Transform = { Position = vector.create(0, 0, 20) },
        RigidBody = { Type = 2, Mass = 5, EnableGravity = false },
        Collider = { Shape = 1, Radius = 1 },
    })
    sim.set_velocity(target, vector.create(0, 0, 0))
    sim.attach(gun, "ray_gun", { Impulse = 50 })
    sim.step(1)
    sim.input_tap("fire")
    sim.step(2)
    assert_near(scene.get(target, "Velocity", "Linear"), vector.create(0, 0, 10), 1e-3)
    local hits = sim.events("ray_hit")
    assert_eq(#hits, 1)
    assert_eq(hits[1].args[1], target)
    assert_near(hits[1].args[2], 19, 1e-3)
end)

test("ray gun ignores bodies outside the layer mask", function()
    local gun = sim.spawn("Gun")
    sim.spawn("Target", {
        Transform = { Position = vector.create(0, 0, 20) },
        RigidBody = { Type = 2, Mass = 5, EnableGravity = false },
        Collider = { Shape = 1, Radius = 1, Layer = 4 },
    })
    sim.attach(gun, "ray_gun", { Mask = 3 })
    sim.step(1)
    sim.input_tap("fire")
    sim.step(2)
    assert_eq(#sim.events("ray_hit"), 0)
end)
