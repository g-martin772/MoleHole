local dvec = require("lib/dvec")
local dq = require("lib/dquat")

local function seq(n)
    local out = {}
    for i = 1, n do out[i] = dvec3.new(i * 1e11, -i, 0.5) end
    return out
end

test("wrapper re-exports the core functions and constants", function()
    assert_eq(dvec.new(1, 2, 3) == dvec3.new(1, 2, 3), true)
    assert_eq(dvec.X + dvec.Y + dvec.Z == dvec.ONE, true)
    assert_eq(dvec.ZERO == dvec3.new(), true)
    assert_eq(dvec.dot(dvec.X, dvec.Y), 0)
end)

test("sum, project, reflect, clamp and move_towards keep 1e11 m precision", function()
    local total = dvec.sum(seq(3))
    assert_eq(total == dvec3.new(6e11, -6, 1.5), true)
    assert_near(dvec.project(dvec3.new(1e11, 5e-3, 0), dvec.X), dvec3.new(1e11, 0, 0), 1e-4)
    assert_near(dvec.reflect(dvec3.new(1, -1, 0), dvec.Y), dvec3.new(1, 1, 0), 1e-15)
    assert_near(dvec.length(dvec.clamp_length(dvec3.new(3e11, 4e11, 0), 10)), 10, 1e-12)
    assert_eq(dvec.clamp_length(dvec3.new(3, 4, 0), 10) == dvec3.new(3, 4, 0), true)
    local from = dvec3.new(1e11, 0, 0)
    local step = dvec.move_towards(from, dvec3.new(1e11 + 1, 0, 0), 0.25)
    assert_eq(step.x - 1e11, 0.25)
    assert_eq(dvec.move_towards(from, dvec3.new(1e11 + 1, 0, 0), 5) == dvec3.new(1e11 + 1, 0, 0), true)
end)

test("angle, near and table conversion", function()
    assert_near(dvec.angle(dvec.X, dvec.Y), math.pi / 2, 1e-15)
    assert_eq(dvec.angle(dvec.ZERO, dvec.X), 0)
    assert_eq(dvec.near(dvec3.new(1e11, 0, 0), dvec3.new(1e11 + 1e-3, 0, 0), 2e-3), true)
    assert_eq(dvec.near(dvec3.new(1e11, 0, 0), dvec3.new(1e11 + 1e-2, 0, 0), 2e-3), false)
    local t = dvec.to_table(dvec3.new(1e11 + 0.5, 2, 3))
    assert_eq(dvec.from_table(t) == dvec3.new(1e11 + 0.5, 2, 3), true)
end)

test("read returns the scene value as a dvec3 and writes survive set64", function()
    local ship = sim.spawn("Ship", { Transform = { Position = dvec3.new(1.5e11 + 1e-3, 0, 2e11) } })
    local p = dvec.read(ship, "Transform", "Position")
    assert_eq(typeof(p), "dvec3")
    assert_eq(p.x, 1.5e11 + 1e-3)
    assert(scene.set64(ship, "Transform", "Position", p + dvec3.new(0.5, 0, 1e-3)))
    sim.step(1)
    assert_eq(scene.get64(ship, "Transform", "Position").x, 1.5e11 + 1e-3 + 0.5)
    assert_eq(scene.get64(ship, "Transform", "Position").z, 2e11 + 1e-3)
end)

test("from_to rotates one direction onto another", function()
    local a, b = dvec3.new(1, 0, 0), dvec3.new(0, 0, 1)
    assert_near(dquat.rotate(dq.from_to(a, b), a), b, 1e-15)
    assert_near(dquat.rotate(dq.from_to(a, a), a), a, 1e-15)
    assert_near(dquat.rotate(dq.from_to(a, -a), a), -a, 1e-12)
    local arbitrary, target = dvec3.new(0.3, -2, 5), dvec3.new(-4, 1, 0.1)
    local q = dq.from_to(arbitrary, target)
    assert_near(dvec3.normalize(dquat.rotate(q, arbitrary)), dvec3.normalize(target), 1e-14)
end)

test("look_rotation faces -z at identity, the target elsewhere", function()
    assert_near(dq.look_rotation(dvec3.new(0, 0, -1)), dq.IDENTITY, 1e-14)
    local forward = dvec3.normalize(dvec3.new(1, 2, -3))
    local q = dq.look_rotation(forward)
    assert_near(dq.forward(q), forward, 1e-14)
    assert_near(dvec3.dot(dq.right(q), dq.up(q)), 0, 1e-14)
    assert(dq.up(q).y > 0, "up stays upright")
    assert_near(dq.forward(dq.look_rotation(dvec3.new(0, 1, 0))), dvec3.new(0, 1, 0), 1e-14)
    assert_near(dq.forward(dq.look_rotation(dvec3.new(-5, 0, 0), dvec3.new(0, 0, 1))), dvec3.new(-1, 0, 0), 1e-14)
    assert_near(dquat.length(q), 1, 1e-14)
end)

test("angle and rotate_towards step along the short arc", function()
    local a = dq.IDENTITY
    local b = dquat.from_axis_angle(dvec3.new(0, 1, 0), 1)
    assert_near(dq.angle(a, b), 1, 1e-12)
    local step = dq.rotate_towards(a, b, 0.25)
    assert_near(dq.angle(a, step), 0.25, 1e-12)
    assert_eq(dq.rotate_towards(a, b, 2) == b, true)
end)

test("set_length and perpendicular", function()
    assert_near(dvec.set_length(dvec3.new(3e11, 4e11, 0), 10), dvec3.new(6, 8, 0), 1e-9)
    assert_eq(dvec.set_length(dvec.ZERO, 5) == dvec.ZERO, true)
    for _, v in { dvec.X, dvec.Y, dvec.Z, dvec3.new(1e11, 2, -3), dvec3.new(0.3, -2, 5), dvec3.new(1e-9, 1e-9, 1e-9) } do
        local p = dvec.perpendicular(v)
        assert_near(dvec.length(p), 1, 1e-12)
        assert_near(dvec3.dot(p, dvec3.normalize(v)), 0, 1e-12)
    end
end)

test("signed_angle follows the axis handedness", function()
    assert_near(dvec.signed_angle(dvec.X, dvec.Y, dvec.Z), math.pi / 2, 1e-15)
    assert_near(dvec.signed_angle(dvec.X, dvec.Y, -dvec.Z), -math.pi / 2, 1e-15)
    assert_near(dvec.signed_angle(dvec.X, dvec.X, dvec.Z), 0, 1e-15)
end)

test("rotate_towards turns the direction and moves the length", function()
    local from, to = dvec3.new(2, 0, 0), dvec3.new(0, 0, -6)
    local step = dvec.rotate_towards(from, to, 0.5, 1)
    assert_near(dvec.angle(from, step), 0.5, 1e-12)
    assert_near(dvec.length(step), 3, 1e-12)
    assert_near(dvec.rotate_towards(from, to, 4, 100), to, 1e-12)
    assert_near(dvec.rotate_towards(from, from, 1, 0.5), from, 1e-12)
    assert_near(dvec.rotate_towards(dvec.ZERO, to, 1, 2), dvec3.new(0, 0, -2), 1e-12)
    assert_near(dvec.rotate_towards(from, dvec.ZERO, 1, 0.5), dvec3.new(1.5, 0, 0), 1e-12)
    local far = dvec.rotate_towards(dvec3.new(1e11, 0, 0), dvec3.new(0, 1e11, 0), 0.25, 0)
    assert_near(dvec.length(far), 1e11, 1)
    assert_near(dvec.angle(dvec.X, far), 0.25, 1e-12)
end)

test("dvec damp and smooth_damp reach the target at 1e11 m", function()
    local target = dvec3.new(1e11 + 50, 0, 0)
    local p, v = dvec3.new(1e11, 0, 0), dvec.ZERO
    for i = 1, 600 do p, v = dvec.smooth_damp(p, target, v, 0.3, 1 / 60) end
    assert_near(p, target, 1e-3)
    local q = dvec3.new(1e11, 0, 0)
    for i = 1, 600 do q = dvec.damp(q, target, 4, 1 / 60) end
    assert_near(q, target, 1e-3)
end)

test("rotation vectors round trip", function()
    for _, axis_angle in { { dvec3.new(0, 1, 0), 0.7 }, { dvec3.new(1, 2, 3), 2.9 }, { dvec3.new(0, 0, 1), 1e-9 }, { dvec3.new(1, 0, 0), 3.1 } } do
        local q = dquat.from_axis_angle(dvec3.normalize(axis_angle[1]), axis_angle[2])
        local rv = dq.to_rotation_vector(q)
        assert_near(dvec.length(rv), axis_angle[2], 1e-9)
        assert_near(dq.angle(dq.from_rotation_vector(rv), q), 0, 1e-7)
    end
    local flipped = dquat.new(0, -math.sin(0.35), 0, -math.cos(0.35))
    assert_near(dq.to_rotation_vector(flipped), dvec3.new(0, 0.7, 0), 1e-12)
    assert_eq(dq.to_rotation_vector(dq.IDENTITY) == dvec.ZERO, true)
    assert_near(dq.from_rotation_vector(dvec.ZERO), dq.IDENTITY, 1e-15)
end)

test("delta and integrate are inverse", function()
    local a = dquat.from_euler(10, 20, 30)
    local b = dquat.from_euler(-40, 75, 5)
    local omega = dq.delta(a, b)
    assert_near(dq.angle(dq.integrate(a, omega, 1), b), 0, 1e-9)
    assert_near(dq.angle(dq.integrate(a, omega / 4, 4), b), 0, 1e-7)
    local q = dq.IDENTITY
    for i = 1, 1000 do q = dq.integrate(q, dvec3.new(0.1, 0.2, -0.3), 1 / 60) end
    assert_near(dquat.length(q), 1, 1e-12)
    assert_near(dq.angle(q, dq.from_rotation_vector(dvec3.new(0.1, 0.2, -0.3) * (1000 / 60))), 0, 1e-9)
end)

test("look_at faces the target", function()
    local from, target = dvec3.new(1e11, 0, 0), dvec3.new(1e11 + 3, 4, 0)
    assert_near(dq.forward(dq.look_at(from, target)), dvec3.normalize(target - from), 1e-9)
end)

test("quaternion damp and smooth_damp converge along the short arc", function()
    local target = dquat.from_axis_angle(dvec3.new(0, 1, 0), 3)
    local q = dq.IDENTITY
    for i = 1, 600 do q = dq.damp(q, target, 5, 1 / 60) end
    assert_near(dq.angle(q, target), 0, 1e-6)
    local p, w = dq.IDENTITY, dvec.ZERO
    local previous = dq.angle(p, target)
    for i = 1, 600 do
        p, w = dq.smooth_damp(p, target, w, 0.4, 1 / 60)
        local remaining = dq.angle(p, target)
        assert(remaining <= previous + 1e-9, "overshoot at " .. i)
        previous = remaining
    end
    assert_near(previous, 0, 1e-4)
    assert_near(dquat.length(p), 1, 1e-12)
    local flipped = dquat.new(-target.x, -target.y, -target.z, -target.w)
    local r, rw = dq.smooth_damp(dq.IDENTITY, flipped, dvec.ZERO, 0.4, 1 / 60)
    assert_near(dq.angle(r, dq.IDENTITY), 0, 0.2)
    assert(dvec.length(rw) > 0)
    local kept, same = dq.smooth_damp(dq.IDENTITY, target, dvec3.new(0, 1, 0), 0.4, 0)
    assert_eq(kept == dq.IDENTITY, true)
    assert_eq(same == dvec3.new(0, 1, 0), true)
end)
