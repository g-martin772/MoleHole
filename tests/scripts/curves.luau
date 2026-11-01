local curves = require("lib/curves")

local names = { "linear", "smoothstep", "smootherstep", "in_quad", "out_quad", "in_out_quad", "in_cubic", "out_cubic", "in_out_cubic", "in_sine", "out_sine", "in_out_sine" }

test("every easing maps 0 to 0, 1 to 1 and never decreases", function()
    for _, name in names do
        assert_near(curves.ease(name, 0), 0, 1e-15, name)
        assert_near(curves.ease(name, 1), 1, 1e-15, name)
        local previous = 0
        for i = 1, 100 do
            local value = curves.ease(name, i / 100)
            assert(value >= previous - 1e-15, name .. " decreased at " .. i)
            previous = value
        end
    end
end)

test("easing values and clamping", function()
    assert_eq(curves.ease("smoothstep", 0.5), 0.5)
    assert_eq(curves.ease("in_quad", 0.5), 0.25)
    assert_eq(curves.ease("out_quad", 0.5), 0.75)
    assert_eq(curves.ease("in_out_cubic", 0.5), 0.5)
    assert_near(curves.ease("in_sine", 0.5), 1 - math.sqrt(0.5), 1e-15)
    assert_eq(curves.ease("in_cubic", -3), 0)
    assert_eq(curves.ease("out_cubic", 7), 1)
    expect_error(function() curves.ease("bounce", 0.5) end, "unknown easing 'bounce'")
end)

test("engine easing names match gpp.ease", function()
    for _, name in { "linear", "easeIn", "easeOut", "easeInOut", "cubicIn", "cubicOut", "cubicInOut", "smoothstep" } do
        for i = 0, 10 do
            assert_near(curves.ease(name, i / 10), gpp.ease(name, i / 10), 1e-6, name)
        end
    end
end)

test("lerp works on numbers, vectors, dvec3 and dquat", function()
    assert_eq(curves.lerp(2, 6, 0.25), 3)
    assert_eq(curves.lerp(vector.create(0, 0, 0), vector.create(2, 4, 6), 0.5), vector.create(1, 2, 3))
    local far = curves.lerp(dvec3.new(1e11, 0, 0), dvec3.new(1e11 + 4, 0, 0), 0.25)
    assert_eq(far.x - 1e11, 1)
    local q = curves.lerp(dquat.new(0, 0, 0, 1), dquat.from_axis_angle(dvec3.new(0, 1, 0), 1), 0.5)
    assert_near(dquat.to_euler(q).y, math.deg(0.5), 1e-9)
    assert_eq(curves.lerp(1, 3, 2), 5)
end)

test("inverse_lerp and remap handle equal bounds", function()
    assert_eq(curves.inverse_lerp(2, 4, 3), 0.5)
    assert_eq(curves.inverse_lerp(5, 5, 9), 0)
    assert_eq(curves.remap(5, 0, 10, 100, 200), 150)
    assert_eq(curves.remap(-5, 0, 10, 100, 200), 50)
    assert_eq(curves.clamp01(1.5), 1)
end)

test("bezier hits its end points and matches the cubic formula", function()
    local p = { 0, 0, 1, 1 }
    assert_eq(curves.bezier(p, 0), 0)
    assert_eq(curves.bezier(p, 1), 1)
    assert_near(curves.bezier(p, 0.5), 0.5, 1e-15)
    assert_near(curves.bezier(p, 0.25), 3 * 0.75 * 0.0625 + 0.015625, 1e-15)
    assert_eq(curves.bezier({ 7 }, 0.3), 7)
    assert_eq(curves.bezier({ 2, 4 }, 0.5), 3)
    local v = { vector.create(0, 0, 0), vector.create(1, 2, 0), vector.create(3, 2, 0), vector.create(4, 0, 0) }
    assert_near(curves.bezier(v, 0.5), vector.create(2, 1.5, 0), 1e-6)
    expect_error(function() curves.bezier({}, 0.5) end, "no control points")
end)

test("bezier tangent is n-1 times the end differences", function()
    local p = { 0, 1, 5, 6 }
    assert_eq(curves.bezier_tangent(p, 0), 3)
    assert_eq(curves.bezier_tangent(p, 1), 3)
    local h = 1e-6
    local numeric = (curves.bezier(p, 0.4 + h) - curves.bezier(p, 0.4 - h)) / (2 * h)
    assert_near(curves.bezier_tangent(p, 0.4), numeric, 1e-6)
    expect_error(function() curves.bezier_tangent({ 1 }, 0.5) end, "at least 2")
end)

test("hermite interpolates positions and tangents", function()
    assert_eq(curves.hermite(1, 4, 9, -2, 0), 1)
    assert_near(curves.hermite(1, 4, 9, -2, 1), 9, 1e-15)
    assert_eq(curves.hermite_tangent(1, 4, 9, -2, 0), 4)
    assert_near(curves.hermite_tangent(1, 4, 9, -2, 1), -2, 1e-15)
    local h = 1e-6
    local numeric = (curves.hermite(1, 4, 9, -2, 0.3 + h) - curves.hermite(1, 4, 9, -2, 0.3 - h)) / (2 * h)
    assert_near(curves.hermite_tangent(1, 4, 9, -2, 0.3), numeric, 1e-6)
    local far = curves.hermite(dvec3.new(1e11, 0, 0), dvec3.new(1, 0, 0), dvec3.new(1e11 + 1, 0, 0), dvec3.new(1, 0, 0), 0.5)
    assert_near(far.x - 1e11, 0.5, 1e-4)
end)

local function square()
    return { vector.create(0, 0, 0), vector.create(4, 0, 0), vector.create(4, 4, 0), vector.create(0, 4, 0) }
end

test("an open spline passes through every point", function()
    local spline = curves.spline(square())
    assert_eq(curves.spline_segments(spline), 3)
    for i = 0, 2 do
        assert_near(curves.spline_eval(spline, i), spline.points[i + 1], 1e-9)
    end
    assert_near(curves.spline_eval(spline, 3), spline.points[4], 1e-6)
    assert_near(curves.spline_eval(spline, -5), spline.points[1], 1e-9)
    assert_near(curves.spline_eval(spline, 99), spline.points[4], 1e-6)
end)

test("a closed spline wraps and has continuous tangents", function()
    local spline = curves.spline(square(), { closed = true })
    assert_eq(curves.spline_segments(spline), 4)
    assert_near(curves.spline_eval(spline, 4), spline.points[1], 1e-9)
    assert_near(curves.spline_eval(spline, 4.5), curves.spline_eval(spline, 0.5), 1e-6)
    assert_near(curves.spline_eval(spline, -0.5), curves.spline_eval(spline, 3.5), 1e-6)
    for i = 0, 3 do
        local before = curves.spline_tangent(spline, i - 1e-9)
        local after = curves.spline_tangent(spline, i + 1e-9)
        assert_near(before, after, 1e-5)
    end
end)

test("tension 1 gives zero tangents at the points", function()
    local spline = curves.spline(square(), { tension = 1 })
    assert_near(curves.spline_tangent(spline, 1), vector.create(0, 0, 0), 1e-9)
    assert_near(curves.spline_eval(spline, 1), spline.points[2], 1e-9)
    expect_error(function() curves.spline({ vector.create(0, 0, 0) }) end, "at least 2")
end)

test("a spline through dvec3 points keeps precision at 1e11 m", function()
    local points = {}
    for i = 1, 4 do points[i] = dvec3.new(1e11 + i * 0.25, -2e11, 3e11 + i * i) end
    local spline = curves.spline(points)
    for i = 0, 2 do
        assert_eq(curves.spline_eval(spline, i) == points[i + 1], true)
    end
    assert_near(curves.spline_eval(spline, 1.5), (points[2] + points[3]) * 0.5, 1)
    assert_near(curves.spline_tangent(spline, 1.5).x, 0.25, 1e-3)
end)

test("arc-length reparametrisation moves at constant speed", function()
    local points = { vector.create(0, 0, 0), vector.create(1, 0, 0), vector.create(3, 0, 0), vector.create(10, 0, 0) }
    local spline = curves.spline(points)
    local arc = curves.spline_table(spline, 64)
    assert_near(arc.length, 10, 1e-6)
    for i = 0, 20 do
        local s = arc.length * i / 20
        assert_near(curves.spline_at_length(spline, arc, s).x, s, 2e-3, "s=" .. s)
    end
    assert_near(curves.spline_at_length(spline, arc, -4), points[1], 1e-9)
    assert_near(curves.spline_at_length(spline, arc, 99), points[4], 1e-4)
end)

test("equal arc steps on a curve give equal chords", function()
    local circle = {}
    for i = 0, 7 do circle[i + 1] = vector.create(math.cos(i * math.pi / 4), math.sin(i * math.pi / 4), 0) end
    local spline = curves.spline(circle, { closed = true })
    local arc = curves.spline_table(spline, 64)
    assert_near(arc.length, 2 * math.pi, 0.05)
    local step = arc.length / 40
    local previous = curves.spline_at_length(spline, arc, 0)
    for i = 1, 40 do
        local point = curves.spline_at_length(spline, arc, step * i)
        assert_near(vector.magnitude(point - previous), step, step * 0.02, "chord " .. i)
        previous = point
    end
    assert_near(curves.spline_at_length(spline, arc, arc.length + step), curves.spline_at_length(spline, arc, step), 1e-6)
end)

test("length_at_u inverts u_at_length", function()
    local arc = curves.arc_table(function(u) return vector.create(u * u, 0, 0) end, 0, 3, 200)
    assert_near(arc.length, 9, 1e-3)
    for _, s in { 0, 1, 4.5, 8.9, 9 } do
        assert_near(curves.length_at_u(arc, curves.u_at_length(arc, s)), s, 1e-9)
    end
    assert_eq(curves.length_at_u(arc, -1), 0)
    assert_eq(curves.length_at_u(arc, 50), arc.length)
end)

local function keys()
    return { { t = 1, v = 10, mode = "linear" }, { t = 3, v = 20, mode = "step" }, { t = 4, v = 40, mode = "smoothstep" }, { t = 6, v = 0 } }
end

test("track modes: linear, step, eased, clamped ends", function()
    local track = curves.track(keys())
    assert_eq(curves.track_duration(track), 5)
    assert_eq(curves.track_eval(track, -10), 10)
    assert_eq(curves.track_eval(track, 1), 10)
    assert_eq(curves.track_eval(track, 2), 15)
    assert_eq(curves.track_eval(track, 3), 20)
    assert_eq(curves.track_eval(track, 3.99), 20)
    assert_eq(curves.track_eval(track, 4), 40)
    assert_eq(curves.track_eval(track, 5), 20)
    assert_eq(curves.track_eval(track, 6), 0)
    assert_eq(curves.track_eval(track, 100), 0)
end)

test("track wrap modes", function()
    local loop = curves.track({ { t = 0, v = 0 }, { t = 2, v = 4 } }, { wrap = "loop" })
    assert_eq(curves.track_eval(loop, 3), 2)
    assert_eq(curves.track_eval(loop, -1), 2)
    assert_eq(curves.track_eval(loop, 4.5), 1)
    local pingpong = curves.track({ { t = 0, v = 0 }, { t = 2, v = 4 } }, { wrap = "pingpong" })
    assert_eq(curves.track_eval(pingpong, 3), 2)
    assert_eq(curves.track_eval(pingpong, 2), 4)
    assert_eq(curves.track_eval(pingpong, 5), 2)
    assert_eq(curves.track_eval(pingpong, 4), 0)
end)

test("cubic track reproduces a line and is smooth through the keys", function()
    local line = curves.track({ { t = 0, v = 0, mode = "cubic" }, { t = 1, v = 1, mode = "cubic" }, { t = 3, v = 3, mode = "cubic" }, { t = 4, v = 4, mode = "cubic" } })
    for i = 0, 40 do
        assert_near(curves.track_eval(line, i / 10), i / 10, 1e-12)
    end
    local wave = curves.track({ { t = 0, v = 0, mode = "cubic" }, { t = 1, v = 2, mode = "cubic" }, { t = 2, v = 0, mode = "cubic" }, { t = 3, v = 2, mode = "cubic" } })
    assert_eq(curves.track_eval(wave, 1), 2)
    local left = curves.track_eval(wave, 1 - 1e-6)
    local right = curves.track_eval(wave, 1 + 1e-6)
    assert_near((right - left) / 2e-6, 0, 1e-4)
end)

test("tracks hold vectors, dvec3 and quaternions", function()
    local v = curves.track({ { t = 0, v = vector.create(0, 0, 0) }, { t = 2, v = vector.create(2, 4, 6) } })
    assert_eq(curves.track_eval(v, 1), vector.create(1, 2, 3))
    local d = curves.track({ { t = 0, v = dvec3.new(1e11, 0, 0) }, { t = 2, v = dvec3.new(1e11 + 2, 0, 0) } })
    assert_eq(curves.track_eval(d, 1).x - 1e11, 1)
    local q = curves.track({ { t = 0, v = dquat.new(0, 0, 0, 1) }, { t = 1, v = dquat.from_axis_angle(dvec3.new(0, 0, 1), 1), mode = "cubic" } })
    assert_near(dquat.to_euler(curves.track_eval(q, 0.5)).z, math.deg(0.5), 1e-6)
end)

test("track validation", function()
    expect_error(function() curves.track({}) end, "no keys")
    expect_error(function() curves.track({ { t = 1, v = 0 }, { t = 1, v = 1 } }) end, "must increase")
    expect_error(function() curves.track_eval(curves.track({ { t = 0, v = 0, mode = "wobble" }, { t = 1, v = 1 } }), 0.5) end, "unknown easing")
    assert_eq(curves.track_eval(curves.track({ { t = 5, v = 3 } }), 0), 3)
    assert_eq(curves.track_eval(curves.track({ { t = 5, v = 3 } }, { wrap = "loop" }), 9), 3)
end)

test("damp is frame-rate independent", function()
    local one = curves.damp(0, 10, 3, 0.1)
    local two = curves.damp(curves.damp(0, 10, 3, 0.05), 10, 3, 0.05)
    assert_near(one, two, 1e-12)
    assert_eq(curves.damp(2, 8, 5, 0), 2)
    assert_near(curves.damp(vector.create(0, 0, 0), vector.create(10, 0, 0), 2, 1e9), vector.create(10, 0, 0), 1e-6)
end)

test("smooth_damp converges without overshoot", function()
    local x, v = 0, 3
    local peak = 0
    for i = 1, 600 do
        x, v = curves.smooth_damp(x, 10, v, 0.5, 1 / 60)
        peak = math.max(peak, x)
    end
    assert_near(x, 10, 1e-3)
    assert_near(v, 0, 1e-3)
    assert(peak <= 10 + 1e-9, "overshoot " .. peak)
end)

test("smooth_damp caps speed and handles vectors, dvec3 and zero dt", function()
    local x, v = 0, 0
    local fastest = 0
    for i = 1, 300 do
        x, v = curves.smooth_damp(x, 1000, v, 1, 1 / 60, 5)
        fastest = math.max(fastest, math.abs(v))
    end
    assert(fastest <= 5 + 1e-9, "speed " .. fastest)
    assert(x > 10, "still moving")
    local p, pv = vector.create(0, 0, 0), vector.create(0, 0, 0)
    for i = 1, 600 do p, pv = curves.smooth_damp(p, vector.create(3, -4, 5), pv, 0.4, 1 / 60) end
    assert_near(p, vector.create(3, -4, 5), 1e-3)
    local far, far_v = dvec3.new(1e11, 0, 0), dvec3.new(0, 0, 0)
    local target = dvec3.new(1e11 + 100, 5, 0)
    for i = 1, 900 do far, far_v = curves.smooth_damp(far, target, far_v, 0.4, 1 / 60) end
    assert_near(far, target, 1e-3)
    local same, kept = curves.smooth_damp(4, 9, 2, 0.5, 0)
    assert_eq(same, 4)
    assert_eq(kept, 2)
    assert_eq(typeof(curves.zero_like(far)), "dvec3")
    assert_eq(curves.zero_like(vector.create(1, 2, 3)), vector.zero)
end)

test("smooth_damp stops on the target when the step would pass it", function()
    local x, v = curves.smooth_damp(9.999, 10, 100, 0.1, 1)
    assert_eq(x, 10)
    assert_eq(v, 0)
end)

local function trace()
    local spline = curves.spline({ vector.create(0, 0, 0), vector.create(5, 2, -1), vector.create(9, -3, 4), vector.create(2, 8, 8) })
    local arc = curves.spline_table(spline)
    local sum = 0
    for i = 0, 500 do
        local p = curves.spline_at_length(spline, arc, arc.length * i / 500)
        sum += p.x * 1.5 + p.y * 2.5 + p.z * 3.5
    end
    return sum
end

test("curve evaluation is deterministic", function()
    assert_eq(trace(), trace())
end)

test("a script following a track tracks the sim clock", function()
    sim.define_script("follower", [[
local curves = require("lib/curves")
local track = curves.track({
    { t = 0, v = vector.create(0, 0, 0), mode = "smoothstep" },
    { t = 2, v = vector.create(10, 0, 0), mode = "cubic" },
    { t = 5, v = vector.create(10, 6, 0) },
})
return {
    OnTick = function(self)
        scene.set(self.entity, "Transform", "Position", curves.track_eval(track, time.sim_time()))
    end,
}
]])
    sim.set_dt(1 / 64)
    local a, b = sim.spawn("A"), sim.spawn("B")
    sim.attach(a, "follower")
    sim.attach(b, "follower")
    sim.step(160)
    local expected = curves.track_eval(curves.track({
        { t = 0, v = vector.create(0, 0, 0), mode = "smoothstep" },
        { t = 2, v = vector.create(10, 0, 0), mode = "cubic" },
        { t = 5, v = vector.create(10, 6, 0) },
    }), time.sim_time())
    assert_eq(scene.get(a, "Transform", "Position"), scene.get(b, "Transform", "Position"))
    assert_near(scene.get(a, "Transform", "Position"), expected, 0.2)
end)
