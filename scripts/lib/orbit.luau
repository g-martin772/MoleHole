local orbit = {}

local TAU = 2 * math.pi
local ECC_EPS = 1e-9
local DEGENERATE = 1e-12
local X, Y, Z = dvec3.new(1, 0, 0), dvec3.new(0, 1, 0), dvec3.new(0, 0, 1)

function orbit.direction(yawDegrees: number, pitchDegrees: number): (number, number, number)
    local yaw, pitch = math.rad(yawDegrees), math.rad(pitchDegrees)
    return math.cos(pitch) * math.sin(yaw), math.sin(pitch), math.cos(pitch) * math.cos(yaw)
end

local function wrap(angle: number): number
    return angle % TAU
end

local function wrap_pi(angle: number): number
    return (angle + math.pi) % TAU - math.pi
end

local function perpendicular(v: DVec3): DVec3
    local axis = math.abs(v.x) <= math.abs(v.y) and math.abs(v.x) <= math.abs(v.z) and X
        or math.abs(v.y) <= math.abs(v.z) and Y
        or Z
    return dvec3.normalize(dvec3.cross(v, axis))
end

local function asinh(x: number): number
    return math.log(x + math.sqrt(x * x + 1))
end

function orbit.mu(mass: number): number
    return units.const("G") * mass
end

function orbit.circular_speed(mu: number, radius: number): number
    return math.sqrt(mu / radius)
end

function orbit.escape_speed(mu: number, radius: number): number
    return math.sqrt(2 * mu / radius)
end

function orbit.speed_at(mu: number, radius: number, a: number): number
    return math.sqrt(math.max(mu * (2 / radius - 1 / a), 0))
end

function orbit.period(mu: number, a: number): number
    return TAU * math.sqrt(a * a * a / mu)
end

function orbit.precession(mu: number, a: number, e: number): number
    local c = units.const("c")
    return 6 * math.pi * mu / (c * c * a * (1 - e * e))
end

function orbit.launch_circular(mu: number, relative: DVec3, normal: DVec3?): DVec3
    local radius = dvec3.length(relative)
    local direction = dvec3.cross(normal and dvec3.normalize(normal) or Y, relative / radius)
    if dvec3.length_squared(direction) < DEGENERATE then
        direction = perpendicular(relative)
    end
    return dvec3.normalize(direction) * math.sqrt(mu / radius)
end

function orbit.mean_anomaly(e: number, nu: number): number
    if e < 1 then
        local E = 2 * math.atan2(math.sqrt(1 - e) * math.sin(nu / 2), math.sqrt(1 + e) * math.cos(nu / 2))
        return E - e * math.sin(E)
    end
    local t = math.sqrt((e - 1) / (e + 1)) * math.tan(nu / 2)
    local F = math.log((1 + t) / (1 - t))
    return e * math.sinh(F) - F
end

function orbit.true_anomaly(e: number, M: number): number
    if e < 1 then
        local m = wrap_pi(M)
        local E = e < 0.8 and m or (m >= 0 and math.pi or -math.pi)
        for _ = 1, 60 do
            local step = (E - e * math.sin(E) - m) / (1 - e * math.cos(E))
            E -= step
            if math.abs(step) < 1e-15 then
                break
            end
        end
        return 2 * math.atan2(math.sqrt(1 + e) * math.sin(E / 2), math.sqrt(1 - e) * math.cos(E / 2))
    end
    local F = asinh(M / e)
    for _ = 1, 80 do
        local step = (e * math.sinh(F) - F - M) / (e * math.cosh(F) - 1)
        F -= step
        if math.abs(step) < 1e-15 * math.max(1, math.abs(F)) then
            break
        end
    end
    return 2 * math.atan2(math.sqrt(e + 1) * math.sinh(F / 2), math.sqrt(e - 1) * math.cosh(F / 2))
end

local function perifocal_basis(inclination: number, raan: number, argp: number): (DVec3, DVec3)
    local co, so = math.cos(raan), math.sin(raan)
    local cw, sw = math.cos(argp), math.sin(argp)
    local ci, si = math.cos(inclination), math.sin(inclination)
    local px, py, pz = co * cw - so * sw * ci, so * cw + co * sw * ci, sw * si
    local qx, qy, qz = -co * sw - so * cw * ci, -so * sw + co * cw * ci, cw * si
    return dvec3.new(px, pz, -py), dvec3.new(qx, qz, -qy)
end

function orbit.elements(mu: number, r: DVec3, v: DVec3)
    local radius = dvec3.length(r)
    local speed2 = dvec3.length_squared(v)
    local h = dvec3.cross(r, v)
    local hl = dvec3.length(h)
    local energy = speed2 / 2 - mu / radius
    local evec = dvec3.cross(v, h) / mu - r / radius
    local e = dvec3.length(evec)
    local p = hl * hl / mu
    local wdir = hl > DEGENERATE * radius * math.sqrt(speed2) and h / hl or perpendicular(r)
    local pdir = e > ECC_EPS and evec / e or r / radius
    local qdir = dvec3.cross(wdir, pdir)
    local bound = energy < 0
    local a = energy ~= 0 and -mu / (2 * energy) or math.huge
    local inclination = math.acos(math.clamp(wdir.y, -1, 1))
    local sin_i = math.sin(inclination)
    local raan, argp = 0, 0
    if sin_i > 1e-9 then
        raan = wrap(math.atan2(wdir.x, wdir.z))
        argp = wrap(math.atan2(pdir.y, qdir.y))
    else
        argp = wrap(math.atan2(-pdir.z * (wdir.y >= 0 and 1 or -1), pdir.x))
    end
    return {
        mu = mu,
        radius = radius,
        speed = math.sqrt(speed2),
        energy = energy,
        momentum = hl,
        p = p,
        e = e,
        a = a,
        bound = bound,
        periapsis = p / (1 + e),
        apoapsis = bound and p / (1 - e) or nil,
        period = bound and TAU * math.sqrt(a * a * a / mu) or nil,
        nu = wrap(math.atan2(dvec3.dot(r, qdir), dvec3.dot(r, pdir))),
        inclination = inclination,
        raan = raan,
        argp = argp,
        pdir = pdir,
        qdir = qdir,
        wdir = wdir,
    }
end

function orbit.position(el, nu: number): DVec3
    local radius = el.p / (1 + el.e * math.cos(nu))
    return (el.pdir * math.cos(nu) + el.qdir * math.sin(nu)) * radius
end

function orbit.state(mu: number, el, nu: number?): (DVec3, DVec3)
    local e = el.e
    local p = el.p or (el.periapsis and el.periapsis * (1 + e)) or el.a * (1 - e * e)
    local pdir, qdir = el.pdir, el.qdir
    if pdir == nil then
        pdir, qdir = perifocal_basis(el.inclination or 0, el.raan or 0, el.argp or 0)
    end
    local anomaly = nu or el.nu or 0
    local radius = p / (1 + e * math.cos(anomaly))
    local k = math.sqrt(mu / p)
    local r = (pdir * math.cos(anomaly) + qdir * math.sin(anomaly)) * radius
    local v = (pdir * -math.sin(anomaly) + qdir * (e + math.cos(anomaly))) * k
    return r, v
end

function orbit.time_to(el, nu: number): number
    local e = el.e
    local n = math.sqrt(el.mu / math.abs(el.a * el.a * el.a))
    local delta = orbit.mean_anomaly(e, e < 1 and wrap(nu) or wrap_pi(nu)) - orbit.mean_anomaly(e, e < 1 and el.nu or wrap_pi(el.nu))
    if e < 1 then
        delta = wrap(delta)
    end
    return delta / n
end

function orbit.time_to_periapsis(el): number
    return orbit.time_to(el, 0)
end

function orbit.time_to_apoapsis(el): number?
    if not el.bound then
        return nil
    end
    return orbit.time_to(el, math.pi)
end

local function stumpff(z: number): (number, number)
    if math.abs(z) < 1e-6 then
        return 0.5 - z / 24 + z * z / 720, 1 / 6 - z / 120 + z * z / 5040
    elseif z > 0 then
        local s = math.sqrt(z)
        return (1 - math.cos(s)) / z, (s - math.sin(s)) / (s * s * s)
    end
    local s = math.sqrt(-z)
    return (math.cosh(s) - 1) / -z, (math.sinh(s) - s) / (s * s * s)
end

function orbit.propagate(mu: number, r0: DVec3, v0: DVec3, dt: number): (DVec3, DVec3)
    if dt == 0 then
        return r0, v0
    end
    local r0l = dvec3.length(r0)
    local sqrtmu = math.sqrt(mu)
    local alpha = 2 / r0l - dvec3.length_squared(v0) / mu
    local rv = dvec3.dot(r0, v0)
    local sign = dt < 0 and -1 or 1
    if alpha > 1e-14 / r0l then
        local period = TAU / (sqrtmu * alpha ^ 1.5)
        if math.abs(dt) > period then
            dt = math.fmod(dt, period)
        end
    end
    local chi
    if alpha > 1e-14 / r0l then
        chi = sqrtmu * alpha * dt
    elseif alpha < -1e-14 / r0l then
        local a = 1 / alpha
        local argument = -2 * mu * alpha * dt / (rv + sign * math.sqrt(-mu * a) * (1 - r0l * alpha))
        chi = argument > 0 and sign * math.sqrt(-a) * math.log(argument) or sqrtmu * dt / r0l
    else
        chi = sqrtmu * dt / r0l
    end
    for _ = 1, 200 do
        local z = alpha * chi * chi
        local C, S = stumpff(z)
        local F = rv / sqrtmu * chi * chi * C + (1 - alpha * r0l) * chi * chi * chi * S + r0l * chi - sqrtmu * dt
        local dF = rv / sqrtmu * chi * (1 - z * S) + (1 - alpha * r0l) * chi * chi * C + r0l
        local ratio = F / dF
        chi -= ratio
        if math.abs(ratio) < 1e-13 * math.max(1, math.abs(chi)) then
            break
        end
    end
    local z = alpha * chi * chi
    local C, S = stumpff(z)
    local f = 1 - chi * chi / r0l * C
    local g = dt - chi * chi * chi / sqrtmu * S
    local r = r0 * f + v0 * g
    local rl = dvec3.length(r)
    local fdot = sqrtmu / (rl * r0l) * (z * S - 1) * chi
    local gdot = 1 - chi * chi / rl * C
    return r, r0 * fdot + v0 * gdot
end

function orbit.predict(mu: number, r: DVec3, v: DVec3, span: number, count: number): { DVec3 }
    local points = { r }
    for i = 1, count do
        points[i + 1] = (orbit.propagate(mu, r, v, span * i / count))
    end
    return points
end

local function radius_limit_anomaly(el, max_radius: number): number
    local c = (el.p / max_radius - 1) / math.max(el.e, ECC_EPS)
    if c >= 1 then
        return 0
    elseif c <= -1 then
        return math.pi
    end
    return math.acos(c)
end

function orbit.sample(el, options)
    local opts = options or {}
    local eye = opts.eye
    local tolerance = opts.tolerance or 0.004
    local max_step = opts.max_step or TAU / 96
    local budget = math.floor((opts.max_points or 300) / 2)
    local nu0 = wrap_pi(el.nu)
    local limit = math.pi
    if not el.bound then
        limit = math.acos(-1 / el.e) - 1e-3
        if opts.max_radius then
            limit = math.min(limit, radius_limit_anomaly(el, opts.max_radius))
        end
    elseif opts.max_radius and el.apoapsis > opts.max_radius then
        limit = math.min(limit, radius_limit_anomaly(el, opts.max_radius))
    end
    local function walk(direction: number, total: number): { DVec3 }
        local points = {}
        local covered = 0
        local nu = nu0
        local remaining_points = budget
        while covered < total - 1e-12 and remaining_points > 0 do
            local r = orbit.position(el, nu)
            local distance = eye and dvec3.distance(r, eye) or dvec3.length(r)
            local slope = el.e * math.sin(nu) / (1 + el.e * math.cos(nu))
            local length = math.sqrt(1 + slope * slope) * dvec3.length(r)
            local step = math.sqrt(8 * el.p * tolerance * math.max(distance, 1e-6 * el.p)) / math.max(length, 1e-9)
            if remaining_points * max_step <= (total - covered) * 1.1 then
                step = max_step
            end
            step = math.min(step, max_step, total - covered)
            covered += step
            nu += direction * step
            remaining_points -= 1
            points[#points + 1] = orbit.position(el, nu)
        end
        return points
    end
    local full = el.bound and limit == math.pi
    local forward_total = full and limit or math.max(limit - nu0, 0)
    local backward_total = full and limit or math.max(limit + nu0, 0)
    local backward = walk(-1, backward_total)
    local forward = walk(1, forward_total)
    local points = {}
    for i = #backward, 1, -1 do
        points[#points + 1] = backward[i]
    end
    points[#points + 1] = orbit.position(el, nu0)
    for i = 1, #forward do
        points[#points + 1] = forward[i]
    end
    return points
end

function orbit.circularise(mu: number, r: DVec3, v: DVec3): DVec3
    local radius = dvec3.length(r)
    local h = dvec3.cross(r, v)
    local tangent = dvec3.cross(h, r)
    if dvec3.length_squared(tangent) < DEGENERATE * radius * radius * dvec3.length_squared(v) then
        tangent = perpendicular(r)
    end
    return dvec3.normalize(tangent) * math.sqrt(mu / radius) - v
end

function orbit.hohmann(mu: number, r1: number, r2: number)
    local a = (r1 + r2) / 2
    local dv1 = orbit.speed_at(mu, r1, a) - math.sqrt(mu / r1)
    local dv2 = math.sqrt(mu / r2) - orbit.speed_at(mu, r2, a)
    return {
        dv1 = dv1,
        dv2 = dv2,
        dv = math.abs(dv1) + math.abs(dv2),
        a = a,
        transfer_time = math.pi * math.sqrt(a * a * a / mu),
    }
end

function orbit.burn_at_apsis(mu: number, el, at: string, other_radius: number)
    local radius = at == "peri" and el.periapsis or el.apoapsis
    if radius == nil then
        return nil
    end
    local a_new = (radius + other_radius) / 2
    local before = orbit.speed_at(mu, radius, el.a)
    local after = orbit.speed_at(mu, radius, a_new)
    return {
        dv = after - before,
        dt = at == "peri" and orbit.time_to_periapsis(el) or orbit.time_to_apoapsis(el),
        radius = radius,
        speed_before = before,
        speed_after = after,
        energy = -mu / (radius + other_radius),
    }
end

function orbit.plan_circular(mu: number, el, target_radius: number, tolerance: number?)
    local tol = tolerance or 0.002
    if el.bound and math.abs(el.periapsis - target_radius) <= tol * target_radius and math.abs(el.apoapsis - target_radius) <= tol * target_radius then
        return nil
    end
    local at = "peri"
    if el.bound and math.abs(el.apoapsis - target_radius) <= math.abs(el.periapsis - target_radius) then
        at = "apo"
    end
    local burn = orbit.burn_at_apsis(mu, el, at, target_radius)
    if burn == nil or burn.dt == nil or burn.dt < 0 then
        return nil
    end
    if el.e < 1e-3 then
        burn.dt = 0
    end
    burn.at = at
    return burn
end

local function body_mass(entity: Entity): number
    local mass = scene.get(entity, "BlackHole", "Mass")
    if mass ~= nil then
        return mass
    end
    return scene.get(entity, "RigidBody", "Mass") or 0
end

function orbit.body(entity: Entity)
    local mass = body_mass(entity)
    if mass <= 0 then
        return nil
    end
    return {
        entity = entity,
        mass = mass,
        mu = orbit.mu(mass),
        position = scene.get64(entity, "Transform", "Position"),
        velocity = scene.get64(entity, "Velocity", "Linear") or dvec3.new(0, 0, 0),
    }
end

function orbit.dominant(position: DVec3, exclude: Entity?)
    local best, best_pull
    local seen = {}
    local function consider(entity: Entity)
        if entity == exclude or seen[entity] then
            return
        end
        seen[entity] = true
        local mass = body_mass(entity)
        local at = scene.get64(entity, "Transform", "Position")
        local d2 = dvec3.length_squared(position - at)
        if mass <= 0 or d2 <= 0 then
            return
        end
        local pull = mass / d2
        if best_pull == nil or pull > best_pull then
            best, best_pull = { entity = entity, mass = mass, position = at }, pull
        end
    end
    for _, entity in scene.query("BlackHole") do
        consider(entity)
    end
    for _, entity in scene.query("RigidBody") do
        if scene.get(entity, "RigidBody", "Type") == 2 then
            consider(entity)
        end
    end
    if best == nil then
        return nil
    end
    best.mu = orbit.mu(best.mass)
    best.velocity = scene.get64(best.entity, "Velocity", "Linear") or dvec3.new(0, 0, 0)
    best.distance = dvec3.distance(position, best.position)
    return best
end

function orbit.refresh(body)
    body.position = scene.get64(body.entity, "Transform", "Position")
    body.velocity = scene.get64(body.entity, "Velocity", "Linear") or dvec3.new(0, 0, 0)
    return body
end

function orbit.analyse(entity: Entity, body)
    local position = scene.get64(entity, "Transform", "Position")
    local velocity = scene.get64(entity, "Velocity", "Linear") or dvec3.new(0, 0, 0)
    body = body or orbit.dominant(position, entity)
    if body == nil then
        return nil
    end
    local r, v = position - body.position, velocity - body.velocity
    if dvec3.length_squared(r) == 0 then
        return nil
    end
    return { body = body, r = r, v = v, elements = orbit.elements(body.mu, r, v) }
end

function orbit.predict_entity(entity: Entity, span: number, count: number, body)
    local info = orbit.analyse(entity, body)
    if info == nil then
        return nil
    end
    local points = orbit.predict(info.body.mu, info.r, info.v, span, count)
    for i = 1, #points do
        points[i] = points[i] + info.body.position
    end
    return points
end

return orbit
