local orbit = require("lib/orbit")

test("zero yaw and pitch faces +z", function()
    local x, y, z = orbit.direction(0, 0)
    assert_near(x, 0)
    assert_near(y, 0)
    assert_near(z, 1)
end)

test("yaw turns toward +x and pitch lifts toward +y", function()
    local x, y, z = orbit.direction(90, 0)
    assert_near(vector.create(x, y, z), vector.create(1, 0, 0))
    x, y, z = orbit.direction(0, 90)
    assert_near(vector.create(x, y, z), vector.create(0, 1, 0))
    x, y, z = orbit.direction(180, -30)
    assert_near(vector.create(x, y, z), vector.create(0, -0.5, -math.sqrt(3) / 2))
end)

test("the direction is always a unit vector", function()
    for yaw = -360, 360, 37 do
        for pitch = -90, 90, 15 do
            local x, y, z = orbit.direction(yaw, pitch)
            assert_near(math.sqrt(x * x + y * y + z * z), 1, 1e-12, "yaw " .. yaw .. " pitch " .. pitch)
        end
    end
end)

local MU = 3.986004418e14
local TAU = 2 * math.pi

local function circular_state(radius)
    return dvec3.new(radius, 0, 0), dvec3.new(0, 0, -math.sqrt(MU / radius))
end

test("a circular orbit has the closed-form period and apsides", function()
    local radius = 6.778e6
    local r, v = circular_state(radius)
    local el = orbit.elements(MU, r, v)
    assert_rel(el.period, TAU * math.sqrt(radius ^ 3 / MU), 1e-12)
    assert_near(el.e, 0, 1e-9)
    assert_rel(el.periapsis, radius, 1e-9)
    assert_rel(el.apoapsis, radius, 1e-9)
    assert_rel(el.a, radius, 1e-12)
    assert(el.bound)
end)

test("vis-viva gives the speed at both apsides", function()
    local rp, ra = 7.0e6, 2.1e7
    local a = (rp + ra) / 2
    local r = dvec3.new(rp, 0, 0)
    local v = dvec3.new(0, 0, -orbit.speed_at(MU, rp, a))
    local el = orbit.elements(MU, r, v)
    assert_rel(el.periapsis, rp, 1e-10)
    assert_rel(el.apoapsis, ra, 1e-10)
    assert_rel(el.a, a, 1e-10)
    assert_rel(el.e, (ra - rp) / (ra + rp), 1e-10)
    assert_rel(el.speed, math.sqrt(MU * (2 / rp - 1 / a)), 1e-12)
    local at_apo = orbit.propagate(MU, r, v, el.period / 2)
    assert_rel(dvec3.length(at_apo), ra, 1e-9)
    assert_rel(orbit.escape_speed(MU, rp), math.sqrt(2) * orbit.circular_speed(MU, rp), 1e-12)
end)

test("the Hohmann burns match the closed form for LEO to GEO", function()
    local r1, r2 = 6.678e6, 4.2164e7
    local plan = orbit.hohmann(MU, r1, r2)
    local dv1 = math.sqrt(MU / r1) * (math.sqrt(2 * r2 / (r1 + r2)) - 1)
    local dv2 = math.sqrt(MU / r2) * (1 - math.sqrt(2 * r1 / (r1 + r2)))
    assert_rel(plan.dv1, dv1, 1e-12)
    assert_rel(plan.dv2, dv2, 1e-12)
    assert_rel(plan.dv, dv1 + dv2, 1e-12)
    assert(plan.dv > 3.8e3 and plan.dv < 4.0e3)
    assert_rel(plan.transfer_time, math.pi * math.sqrt(((r1 + r2) / 2) ^ 3 / MU), 1e-12)
    local down = orbit.hohmann(MU, r2, r1)
    assert(down.dv1 < 0 and down.dv2 < 0)
    assert_rel(down.dv, plan.dv, 1e-12)
end)

test("state vectors and Kepler elements round trip", function()
    local cases = {
        { a = 1.2e7, e = 0.3, inclination = 0.7, raan = 1.1, argp = 2.3, nu = 0.9 },
        { a = 8.0e6, e = 0.05, inclination = 2.9, raan = 5.5, argp = 0.2, nu = 4.0 },
        { a = 2.0e7, e = 0.6, inclination = 0.0, raan = 0.0, argp = 1.3, nu = 5.9 },
        { a = 2.0e7, e = 0.6, inclination = math.pi, raan = 0.0, argp = 1.3, nu = 2.1 },
        { a = -3.0e7, e = 1.8, inclination = 1.2, raan = 0.4, argp = 3.0, nu = 0.5 },
    }
    for index, given in cases do
        local r, v = orbit.state(MU, given)
        local el = orbit.elements(MU, r, v)
        assert_rel(el.a, given.a, 1e-9, "a " .. index)
        assert_near(el.e, given.e, 1e-9, "e " .. index)
        assert_near(el.inclination, given.inclination, 1e-9, "i " .. index)
        assert_near(math.cos(el.nu), math.cos(given.nu), 1e-9, "nu " .. index)
        assert_near(math.sin(el.nu), math.sin(given.nu), 1e-9, "nu " .. index)
        if given.inclination > 1e-6 and given.inclination < math.pi - 1e-6 then
            assert_near(math.cos(el.raan), math.cos(given.raan), 1e-9, "raan " .. index)
            assert_near(math.sin(el.raan), math.sin(given.raan), 1e-9, "raan " .. index)
        end
        assert_near(math.cos(el.argp), math.cos(given.argp), 1e-8, "argp " .. index)
        assert_near(math.sin(el.argp), math.sin(given.argp), 1e-8, "argp " .. index)
        local r2, v2 = orbit.state(MU, el)
        assert_near(r2, r, 1e-3, "r " .. index)
        assert_near(v2, v, 1e-6, "v " .. index)
    end
end)

test("the Kepler equation inverts for ellipses and hyperbolas", function()
    for _, e in { 0, 0.1, 0.7, 0.95, 1.3, 4.0 } do
        local reach = e > 1 and math.acos(-1 / e) - 0.05 or 2.0
        for nu = -math.min(reach, 2.0), math.min(reach, 2.0), 0.37 do
            local M = orbit.mean_anomaly(e, nu)
            assert_near(orbit.true_anomaly(e, M), nu, 1e-10, "e " .. e .. " nu " .. nu)
        end
    end
end)

test("propagation conserves energy and momentum and closes after a period", function()
    local r, v = orbit.state(MU, { a = 1.5e7, e = 0.4, inclination = 0.5, raan = 1.0, argp = 0.3, nu = 1.0 })
    local start = orbit.elements(MU, r, v)
    for _, fraction in { 0.1, 0.5, 0.93, 2.7 } do
        local r2, v2 = orbit.propagate(MU, r, v, start.period * fraction)
        local el = orbit.elements(MU, r2, v2)
        assert_rel(el.energy, start.energy, 1e-10)
        assert_rel(el.momentum, start.momentum, 1e-10)
        assert_near(el.e, start.e, 1e-10)
    end
    local back, back_v = orbit.propagate(MU, r, v, start.period)
    assert_near(back, r, 1e-4)
    assert_near(back_v, v, 1e-7)
end)

test("propagation follows the mean anomaly and hyperbolas stay on their conic", function()
    local given = { a = 1.5e7, e = 0.4, inclination = 0.5, raan = 1.0, argp = 0.3, nu = 1.0 }
    local r, v = orbit.state(MU, given)
    local el = orbit.elements(MU, r, v)
    local n = math.sqrt(MU / el.a ^ 3)
    local dt = 3456.0
    local M = orbit.mean_anomaly(el.e, el.nu) + n * dt
    local expected = orbit.state(MU, el, orbit.true_anomaly(el.e, M))
    assert_near(orbit.propagate(MU, r, v, dt), expected, 1e-3)

    local hr, hv = orbit.state(MU, { a = -3.0e7, e = 1.8, inclination = 1.2, raan = 0.4, argp = 3.0, nu = -1.0 })
    local hel = orbit.elements(MU, hr, hv)
    assert(not hel.bound and hel.period == nil and hel.apoapsis == nil)
    local forward_r, forward_v = orbit.propagate(MU, hr, hv, 5000)
    local after = orbit.elements(MU, forward_r, forward_v)
    assert_rel(after.energy, hel.energy, 1e-10)
    assert_rel(after.momentum, hel.momentum, 1e-10)
    assert_near(orbit.propagate(MU, forward_r, forward_v, -5000), hr, 1e-3)
end)

test("time to the apsides is exact on a circle quarter and an ellipse", function()
    local r, v = orbit.state(MU, { a = 1.5e7, e = 0.4, inclination = 0.0, raan = 0.0, argp = 0.0, nu = 0.0 })
    local el = orbit.elements(MU, r, v)
    assert_near(orbit.time_to_periapsis(el), 0, 1e-6)
    assert_rel(orbit.time_to_apoapsis(el), el.period / 2, 1e-10)
    local later_r, later_v = orbit.propagate(MU, r, v, el.period / 4)
    local later = orbit.elements(MU, later_r, later_v)
    assert_rel(orbit.time_to_apoapsis(later) + el.period / 4, el.period / 2, 1e-9)
    assert_rel(orbit.time_to_periapsis(later), el.period * 0.75, 1e-9)
    local hr, hv = orbit.state(MU, { a = -3.0e7, e = 1.8, inclination = 0.2, raan = 0.4, argp = 3.0, nu = -1.0 })
    assert(orbit.time_to_periapsis(orbit.elements(MU, hr, hv)) > 0)
    local hr2, hv2 = orbit.state(MU, { a = -3.0e7, e = 1.8, inclination = 0.2, raan = 0.4, argp = 3.0, nu = 1.0 })
    assert(orbit.time_to_periapsis(orbit.elements(MU, hr2, hv2)) < 0)
end)

test("circularising burn leaves a circle at the burn radius", function()
    local r, v = orbit.state(MU, { a = 1.5e7, e = 0.4, inclination = 0.5, raan = 1.0, argp = 0.3, nu = 1.0 })
    local dv = orbit.circularise(MU, r, v)
    local el = orbit.elements(MU, r, v + dv)
    assert_near(el.e, 0, 1e-9)
    assert_rel(el.a, dvec3.length(r), 1e-9)
    assert_near(dvec3.dot(dv, r), dvec3.dot(-v, r), 1e-3)
    local radial = orbit.circularise(MU, dvec3.new(7e6, 0, 0), dvec3.new(100, 0, 0))
    assert_rel(dvec3.length(dvec3.new(100, 0, 0) + radial), math.sqrt(MU / 7e6), 1e-9)
end)

test("burns at an apsis move the opposite apsis to the target", function()
    local r, v = orbit.state(MU, { a = 1.5e7, e = 0.4, inclination = 0.3, raan = 1.0, argp = 0.3, nu = 0.0 })
    local el = orbit.elements(MU, r, v)
    local burn = orbit.burn_at_apsis(MU, el, "peri", 3.0e7)
    local tangent = dvec3.normalize(v)
    local after = orbit.elements(MU, r, v + tangent * burn.dv)
    assert_rel(after.periapsis, el.periapsis, 1e-9)
    assert_rel(after.apoapsis, 3.0e7, 1e-9)
    assert_rel(burn.energy, after.energy, 1e-9)
    local apo = orbit.burn_at_apsis(MU, el, "apo", el.apoapsis)
    local at_apo_r, at_apo_v = orbit.propagate(MU, r, v, el.period / 2)
    local circle = orbit.elements(MU, at_apo_r, at_apo_v + dvec3.normalize(at_apo_v) * apo.dv)
    assert_near(circle.e, 0, 1e-9)
    assert_rel(apo.dt, el.period / 2, 1e-9)
end)

test("the circular plan chains a Hohmann transfer and then stops asking", function()
    local r, v = circular_state(7.0e6)
    local el = orbit.elements(MU, r, v)
    local first = orbit.plan_circular(MU, el, 1.4e7)
    assert_eq(first.dt, 0)
    assert_rel(first.dv, orbit.hohmann(MU, 7.0e6, 1.4e7).dv1, 1e-9)
    local transfer = orbit.elements(MU, r, v + dvec3.normalize(v) * first.dv)
    local second = orbit.plan_circular(MU, transfer, 1.4e7)
    assert_eq(second.at, "apo")
    assert_rel(second.dt, transfer.period / 2, 1e-9)
    assert_rel(second.dv, orbit.hohmann(MU, 7.0e6, 1.4e7).dv2, 1e-9)
    assert_eq(orbit.plan_circular(MU, orbit.elements(MU, circular_state(1.4e7)), 1.4e7), nil)
end)

test("launch_circular gives a circular orbit about the chosen normal", function()
    local r = dvec3.new(1e7, 2e6, 0)
    local v = orbit.launch_circular(MU, r, dvec3.new(0, 0, 1))
    local el = orbit.elements(MU, r, v)
    assert_near(el.e, 0, 1e-9)
    assert_near(dvec3.dot(v, r), 0, 1e-3)
    assert_near(dvec3.dot(el.wdir, dvec3.new(0, 0, 1)), 1, 1e-12)
end)

test("the GR precession estimate matches the textbook value for Mercury", function()
    local mu = orbit.mu(1.98847e30)
    local per_orbit = orbit.precession(mu, 5.791e10, 0.2056)
    local arcsec_per_century = math.deg(per_orbit) * 3600 * (36525 / 87.969)
    assert(arcsec_per_century > 42 and arcsec_per_century < 44, arcsec_per_century)
end)

test("a prediction polyline sits on the propagated orbit and is reproducible", function()
    local r, v = orbit.state(MU, { a = 1.5e7, e = 0.4, inclination = 0.5, raan = 1.0, argp = 0.3, nu = 1.0 })
    local el = orbit.elements(MU, r, v)
    local points = orbit.predict(MU, r, v, el.period, 64)
    assert_eq(#points, 65)
    assert_near(points[1], r, 1e-9)
    assert_near(points[65], r, 1e-3)
    for _, p in points do
        assert_near(dvec3.dot(p, el.wdir), 0, 1e-3)
    end
    local again = orbit.predict(MU, r, v, el.period, 64)
    for i = 1, 65 do
        assert_eq(points[i].x, again[i].x)
        assert_eq(points[i].y, again[i].y)
        assert_eq(points[i].z, again[i].z)
    end
end)

test("sampled curves lie on the conic, close for ellipses and refine near the eye", function()
    local r, v = orbit.state(MU, { a = 1.5e7, e = 0.4, inclination = 0.5, raan = 1.0, argp = 0.3, nu = 1.0 })
    local el = orbit.elements(MU, r, v)
    local plain = orbit.sample(el)
    assert_near(plain[1], plain[#plain], 1e-3)
    for _, p in plain do
        local radius = dvec3.length(p)
        local nu = math.atan2(dvec3.dot(p, el.qdir), dvec3.dot(p, el.pdir))
        assert_rel(radius, el.p / (1 + el.e * math.cos(nu)), 1e-9)
        assert_near(dvec3.dot(p, el.wdir), 0, 1e-3)
    end
    local eye = r + dvec3.new(200, 100, 0)
    local refined = orbit.sample(el, { eye = eye })
    assert(#refined <= 300)
    local nearest = math.huge
    for i = 2, #refined do
        local a, b = refined[i - 1], refined[i]
        local mid = (a + b) / 2
        local nu = math.atan2(dvec3.dot(mid, el.qdir), dvec3.dot(mid, el.pdir))
        local sagitta = math.abs(dvec3.length(mid) - el.p / (1 + el.e * math.cos(nu)))
        local closest = math.min(dvec3.distance(a, eye), dvec3.distance(b, eye))
        assert(sagitta <= 0.01 * closest + 1, "segment " .. i .. " sagitta " .. sagitta .. " at " .. closest)
        nearest = math.min(nearest, dvec3.distance(a, b))
    end
    assert(nearest < 1.2e4, nearest)
    assert(#refined > #plain)
    local hr, hv = orbit.state(MU, { a = -3.0e7, e = 1.8, inclination = 1.2, raan = 0.4, argp = 3.0, nu = -1.0 })
    local hyper = orbit.sample(orbit.elements(MU, hr, hv), { max_radius = 2e8 })
    assert(#hyper > 10)
    for _, p in hyper do
        assert(dvec3.length(p) < 2.1e8)
    end
end)

test("the dominant body is the strongest pull and an entity analysis finds its orbit", function()
    local hole = sim.spawn("Hole", { BlackHole = { Mass = 1.0e27 } })
    scene.set64(hole, "Transform", "Position", dvec3.new(1e9, 0, 0))
    local moon = sim.spawn("Moon", { RigidBody = { Type = 2, Mass = 1.0e24, EnableGravity = false }, Collider = { Shape = 1, Radius = 1 } })
    scene.set64(moon, "Transform", "Position", dvec3.new(0, 0, 0))
    local ship = sim.spawn("Ship", { RigidBody = { Type = 2, Mass = 100, EnableGravity = false }, Collider = { Shape = 1, Radius = 1 } })
    scene.set64(ship, "Transform", "Position", dvec3.new(1e9 + 2e6, 0, 0))
    local near_hole = orbit.dominant(dvec3.new(1e9 + 2e6, 0, 0), ship)
    assert_eq(near_hole.entity, hole)
    assert_rel(near_hole.mu, orbit.mu(1.0e27), 1e-6)
    local near_moon = orbit.dominant(dvec3.new(5e6, 0, 0), ship)
    assert_eq(near_moon.entity, moon)
    local v = orbit.launch_circular(near_hole.mu, dvec3.new(2e6, 0, 0), dvec3.new(0, 1, 0))
    sim.set_velocity(ship, vector.create(v.x, v.y, v.z))
    local info = orbit.analyse(ship)
    assert_eq(info.body.entity, hole)
    assert_near(info.elements.e, 0, 1e-5)
    assert_rel(info.elements.a, 2e6, 1e-6)
    local points = orbit.predict_entity(ship, info.elements.period / 4, 4)
    assert_eq(#points, 5)
    assert_rel(dvec3.distance(points[5], dvec3.new(1e9, 0, 0)), 2e6, 1e-5)
end)
