-- Flies the starship along a smooth path and moves the camera along keyframes, both as pure functions of the scene
-- clock (time.sim_time, physical seconds), so Play, a video and an image export at --sim-start show the same pose.
-- Lengths in the tables are gravitational radii of the hole; Unit converts them to metres.

local Unit = 14766.25

-- Ship: waypoints [r_g]; Catmull-Rom through them at constant speed
local waypoints = {
    { 82, 18, 50 }, { 58, 14, 40 }, { 38, 9, 30 }, { 22, 5, 22 }, { 8, 4, 16 }, { -8, 5, 10 },
    { -24, 6, 2 }, { -30, 3, -12 }, { -18, 2, -26 }, { 2, 2, -32 }, { 22, 4, -26 }, { 42, 8, -14 }, { 64, 12, -2 },
}

-- Camera keyframes: time [s], position offset (3), ship weight of the position, look offset (3), ship weight of the look
-- position = offset + weight * shipPosition, linearly blended with smoothstep between keys
local keys = {
    { 0.00, 44, 40, 96, 0.0, -8, 0, 0, 0.15 },
    { 0.50, 38, 32, 86, 0.0, -6, 0, 0, 0.4 },
    { 1.00, 30, 24, 76, 0.0, -4, 0, 0, 0.5 },
    { 1.50, 22, 20, 70, 0.0, -4, 0, 0, 0.55 },
    { 2.00, 14, 18, 64, 0.0, -4, 0, 0, 0.5 },
}

local function catmull(p0: number, p1: number, p2: number, p3: number, t: number): number
    local t2, t3 = t * t, t * t * t
    return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3)
end

local function splinePoint(points, u: number): (number, number, number)
    local n = #points
    local i = math.clamp(math.floor(u), 1, n - 1)
    local t = math.clamp(u - i, 0, 1)
    local a, b, c, d = points[math.max(i - 1, 1)], points[i], points[i + 1], points[math.min(i + 2, n)]
    return catmull(a[1], b[1], c[1], d[1], t), catmull(a[2], b[2], c[2], d[2], t), catmull(a[3], b[3], c[3], d[3], t)
end

local function smooth(t: number): number
    t = math.clamp(t, 0, 1)
    return t * t * (3 - 2 * t)
end

-- Arc length table of the waypoint spline: u (1..n) at every sample, cumulative length in r_g
local samples = {}
local total = 0
do
    local n = #waypoints
    local px, py, pz = splinePoint(waypoints, 1)
    table.insert(samples, { 1, 0 })
    local steps = (n - 1) * 40
    for k = 1, steps do
        local u = 1 + (n - 1) * k / steps
        local x, y, z = splinePoint(waypoints, u)
        total += math.sqrt((x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2)
        table.insert(samples, { u, total })
        px, py, pz = x, y, z
    end
end

local function parameterAt(distance: number): number
    if distance <= 0 then return 1 end
    if distance >= total then return samples[#samples][1] end
    local lo, hi = 1, #samples
    while hi - lo > 1 do
        local mid = (lo + hi) // 2
        if samples[mid][2] <= distance then lo = mid else hi = mid end
    end
    local a, b = samples[lo], samples[hi]
    return a[1] + (b[1] - a[1]) * (distance - a[2]) / math.max(b[2] - a[2], 1e-9)
end

local function shipPose(t: number, speed: number): (number, number, number, number, number, number)
    local u = parameterAt(speed * math.max(t, 0) / Unit)
    local x, y, z = splinePoint(waypoints, u)
    local x2, y2, z2 = splinePoint(waypoints, math.min(u + 0.02, #waypoints))
    local x1, y1, z1 = splinePoint(waypoints, math.max(u - 0.02, 1))
    return x, y, z, x2 - x1, y2 - y1, z2 - z1
end

local function cameraPose(t: number, sx: number, sy: number, sz: number): (number, number, number, number, number, number)
    local first, last = keys[1], keys[#keys]
    if t <= first[1] then t = first[1] end
    if t >= last[1] then t = last[1] end
    local i = 1
    while i < #keys - 1 and t > keys[i + 1][1] do i += 1 end
    local a, b = keys[i], keys[i + 1]
    local w = smooth((t - a[1]) / math.max(b[1] - a[1], 1e-9))
    local function mix(k: number): number return a[k] + (b[k] - a[k]) * w end
    local pw, lw = mix(5), mix(9)
    return mix(2) + pw * sx, mix(3) + pw * sy, mix(4) + pw * sz, mix(6) + lw * sx, mix(7) + lw * sy, mix(8) + lw * sz
end

return {
    properties = {
        { name = "Ship", type = "entity" },
        { name = "Camera", type = "entity" },
        { name = "ShipSpeed", type = "float", unit = "m/s", default = 2.8e6, min = 0, max = 3e8 },
        { name = "ShipYaw", type = "float", default = 180 },
        { name = "TimeOffset", type = "float", default = 0 },
    },

    OnStart = function(self: ScriptSelf)
        self.lastT = nil
    end,

    OnTick = function(self: ScriptSelf, dt: number)
        local props = self.props
        local t = time.sim_time() + props.TimeOffset
        local sx, sy, sz, fx, fy, fz = shipPose(t, props.ShipSpeed)
        if props.Ship ~= nil and scene.exists(props.Ship) then
            scene.set_xyz(props.Ship, "Transform", "Position", sx * Unit, sy * Unit, sz * Unit)
            local euler = scene.look_at(vector.zero, vector.create(fx, fy, fz))
            scene.set(props.Ship, "Transform", "Rotation", vector.create(euler.x, euler.y + props.ShipYaw, euler.z))
        end
        if props.Camera ~= nil and scene.exists(props.Camera) then
            local cx, cy, cz, lx, ly, lz = cameraPose(t, sx, sy, sz)
            scene.set_xyz(props.Camera, "Transform", "Position", cx * Unit, cy * Unit, cz * Unit)
            scene.set(props.Camera, "Transform", "Rotation", scene.look_at(vector.create(cx, cy, cz), vector.create(lx, ly, lz)))
        end
    end,
}
