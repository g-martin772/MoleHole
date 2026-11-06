local curves = require("lib/curves")
local dquat_lib = require("lib/dquat")

local rigs = {}

rigs.SHAKE_RATE = 14
rigs.HANDHELD_RATE = 0.7
rigs.FOV_MIN = 1
rigs.FOV_MAX = 179

local UINT = 4294967296

local function mul32(a: number, b: number): number
    local low = (a % 65536) * b
    local high = ((a // 65536) * b) % 65536
    return (low + high * 65536) % UINT
end

local function hash(seed: number, channel: number, index: number): number
    local h = bit32.bxor(seed % UINT, mul32(channel + 1, 0x9E3779B1))
    h = bit32.bxor(h, mul32(index % UINT, 0x85EBCA6B))
    h = bit32.bxor(h, bit32.rshift(h, 16))
    h = mul32(h, 0x7FEB352D)
    h = bit32.bxor(h, bit32.rshift(h, 15))
    h = mul32(h, 0x846CA68B)
    h = bit32.bxor(h, bit32.rshift(h, 16))
    return h / 2147483648 - 1
end

function rigs.noise(seed: number, channel: number, t: number): number
    local cell = math.floor(t)
    local f = t - cell
    local u = f * f * f * (f * (f * 6 - 15) + 10)
    local a, b = hash(seed, channel, cell), hash(seed, channel, cell + 1)
    return a + (b - a) * u
end

function rigs.fractal(seed: number, channel: number, t: number): number
    return (rigs.noise(seed, channel, t) + 0.5 * rigs.noise(seed, channel + 64, t * 2.13 + 17.3)) / 1.5
end

function rigs.decay(trauma: number, rate: number, dt: number): number
    return math.max(trauma - rate * dt, 0)
end

function rigs.shake_pose(seed: number, clock: number, trauma: number, max_angle: number, max_offset: number): (DQuat, DVec3)
    local k = math.clamp(trauma, 0, 1)
    k = k * k
    local t = clock * rigs.SHAKE_RATE
    local pitch = rigs.noise(seed, 1, t) * k * max_angle
    local yaw = rigs.noise(seed, 2, t) * k * max_angle
    local roll = rigs.noise(seed, 3, t) * k * max_angle
    local offset = dvec3.new(rigs.noise(seed, 4, t), rigs.noise(seed, 5, t), rigs.noise(seed, 6, t)) * (k * max_offset)
    return dquat.from_euler(pitch, yaw, roll), offset
end

function rigs.handheld_pose(seed: number, clock: number, amplitude: number): DQuat
    local t = clock * rigs.HANDHELD_RATE
    return dquat.from_euler(
        rigs.fractal(seed, 11, t) * amplitude,
        rigs.fractal(seed, 12, t) * amplitude,
        rigs.fractal(seed, 13, t) * amplitude * 0.5
    )
end

local function number(value: number?, default: number): number
    return value == nil and default or value
end

function rigs.seed(self, props): number
    local given = number(props.Seed, 0)
    if given ~= 0 then
        return given
    end
    if self.cam_seed == nil then
        self.cam_seed = math.random(1, 1073741824)
    end
    return self.cam_seed
end

function rigs.shake_add(self, amount: number)
    self.trauma = math.clamp((self.trauma or 0) + math.max(amount, 0), 0, 1)
end

function rigs.on_shake(self, name: string, amount: any)
    if name == "camera_shake" and type(amount) == "number" and scene.get(self.entity, "Camera", "Primary") then
        rigs.shake_add(self, amount)
    end
end

function rigs.on_switch(self, name: string, from: any, to: any)
    if name == "camera_switch" and to == self.entity then
        self.from = from
    end
end

local function base_pose(self): (DVec3, DQuat)
    local entity = self.entity
    local position = scene.get64(entity, "Transform", "Position")
    local rotation = scene.get64(entity, "Transform", "Rotation")
    if self.fx_on then
        rotation = dquat.mul(rotation, dquat.inverse(dquat_lib.from_rotation_vector(dvec3.from_vec(self.fx_rot))))
        position = position - rotation * dvec3.from_vec(self.fx_pos)
    end
    return position, rotation
end

rigs.base_pose = base_pose

function rigs.release(self)
    if not self.fx_on then
        return
    end
    local position, rotation = base_pose(self)
    scene.set64(self.entity, "Transform", "Position", position)
    scene.set64(self.entity, "Transform", "Rotation", rotation)
    self.fx_on = false
end

function rigs.forget(self)
    self.fx_on = false
end

function rigs.effects(self, props, dt: number, position: DVec3, rotation: DQuat)
    local clock = (self.fx_clock or 0) + dt
    self.fx_clock = clock
    local trauma = self.trauma or 0
    local handheld = number(props.Handheld, 0)
    local angle, offset = number(props.ShakeAngle, 2), number(props.ShakeOffset, 0)
    if (trauma <= 0 or (angle <= 0 and offset <= 0)) and handheld <= 0 then
        self.trauma = 0
        self.fx_on = false
        return
    end
    local seed = rigs.seed(self, props)
    local turn, shift = dquat_lib.IDENTITY, dvec3.new(0, 0, 0)
    if trauma > 0 then
        turn, shift = rigs.shake_pose(seed, clock, trauma, angle, offset)
        self.trauma = rigs.decay(trauma, number(props.ShakeDecay, 1.5), dt)
    end
    if handheld > 0 then
        turn = dquat.mul(turn, rigs.handheld_pose(seed, clock, handheld))
    end
    local rotvec = dvec3.to_vec(dquat_lib.to_rotation_vector(turn))
    local local_shift = dvec3.to_vec(shift)
    self.fx_rot, self.fx_pos, self.fx_on = rotvec, local_shift, true
    scene.set64(self.entity, "Transform", "Position", position + rotation * dvec3.from_vec(local_shift))
    scene.set64(self.entity, "Transform", "Rotation", dquat.mul(rotation, dquat_lib.from_rotation_vector(dvec3.from_vec(rotvec))))
end

function rigs.aim(yaw: number, pitch: number): DQuat
    return dquat.mul(dquat.from_axis_angle(dvec3.new(0, 1, 0), math.rad(yaw)), dquat.from_axis_angle(dvec3.new(1, 0, 0), math.rad(pitch)))
end

function rigs.read_aim(self)
    if self.yaw == nil then
        local _, rotation = base_pose(self)
        local f = dquat.rotate(rotation, dvec3.new(0, 0, -1))
        self.yaw, self.pitch = math.deg(math.atan2(-f.x, -f.z)), math.deg(math.asin(math.clamp(f.y, -1, 1)))
    end
end

function rigs.turn(self, dyaw: number, dpitch: number)
    self.yaw += dyaw
    self.pitch = math.clamp(self.pitch + dpitch, -89, 89)
end

function rigs.handover(self): boolean
    if self.from == nil then
        return false
    end
    local entity = self.entity
    if scene.exists(self.from) then
        scene.set64(entity, "Transform", "Position", scene.get64(self.from, "Transform", "Position"))
        scene.set64(entity, "Transform", "Rotation", scene.get64(self.from, "Transform", "Rotation"))
    end
    self.from, self.yaw, self.rel = nil, nil, nil
    self.fx_on = false
    return true
end

function rigs.orbit_pose(anchor: DVec3, yaw: number, pitch: number, distance: number, height: number): (DVec3, DQuat)
    local rotation = rigs.aim(yaw, pitch)
    return anchor + rotation * dvec3.new(0, height, distance), rotation
end

function rigs.follow(self, anchor: DVec3, rotation: DQuat, height: number, distance: number, smooth_time: number, dt: number): (DVec3, DVec3)
    local want = rotation * dvec3.new(0, height, distance)
    if self.rel == nil then
        self.rel = dvec3.to_vec((base_pose(self)) - anchor)
    end
    local rel, vel = curves.smooth_damp(dvec3.from_vec(self.rel), want, dvec3.from_vec(self.vel), smooth_time, dt)
    self.rel, self.vel = dvec3.to_vec(rel), dvec3.to_vec(vel)
    scene.set64(self.entity, "Transform", "Position", anchor + rel)
    return rel, anchor + rel
end

function rigs.chase_aim(self, rotation: DQuat, rel: DVec3, look_ahead: number, turn_time: number, dt: number): DQuat
    local aim = dquat_lib.look_rotation(dquat_lib.forward(rotation) * look_ahead - rel, dquat_lib.up(rotation))
    local _, current = base_pose(self)
    local turned, omega = dquat_lib.smooth_damp(current, aim, dvec3.from_vec(self.turn), turn_time, dt)
    self.turn = dvec3.to_vec(omega)
    scene.set64(self.entity, "Transform", "Rotation", turned)
    return turned
end

function rigs.fly_move(self, rotation: DQuat, move: DVec3, anchor: DVec3?): DVec3
    local entity = self.entity
    local position = base_pose(self)
    if anchor ~= nil then
        if self.rel == nil then
            self.rel = dvec3.to_vec(position - anchor)
        end
        local rel = dvec3.from_vec(self.rel) + move
        self.rel = dvec3.to_vec(rel)
        position = anchor + rel
    else
        self.rel = nil
        position = position + move
    end
    scene.set64(entity, "Transform", "Position", position)
    scene.set64(entity, "Transform", "Rotation", rotation)
    return position
end

function rigs.ride_along(anchor: DVec3, rotation: DQuat, offset: DVec3, look: DQuat?): (DVec3, DQuat)
    return anchor + rotation * offset, look and dquat.mul(rotation, look) or rotation
end

function rigs.lead_point(position: DVec3, velocity: DVec3, lead_time: number): DVec3
    return position + velocity * lead_time
end

function rigs.look_at_lead(from: DVec3, position: DVec3, velocity: DVec3, lead_time: number, up: DVec3?): DQuat
    return dquat_lib.look_rotation(rigs.lead_point(position, velocity, lead_time) - from, up)
end

function rigs.subject_width(fov: number, distance: number): number
    return 2 * distance * math.tan(math.rad(fov) / 2)
end

function rigs.dolly_fov(base_fov: number, base_distance: number, distance: number): number
    local half = math.atan(base_distance * math.tan(math.rad(base_fov) / 2) / math.max(distance, 1e-9))
    return math.clamp(math.deg(half * 2), rigs.FOV_MIN, rigs.FOV_MAX)
end

function rigs.dolly_distance(base_fov: number, base_distance: number, fov: number): number
    return base_distance * math.tan(math.rad(base_fov) / 2) / math.tan(math.rad(fov) / 2)
end

function rigs.parse_points(text: string): { DVec3 }
    local out = {}
    for item in string.gmatch(text, "[^;]+") do
        local values = {}
        for token in string.gmatch(item, "[^%s,]+") do
            table.insert(values, tonumber(token))
        end
        if #values == 0 then
            continue
        end
        if #values ~= 3 then
            error("camera_rigs: bad point '" .. item .. "'", 0)
        end
        table.insert(out, dvec3.new(values[1], values[2], values[3]))
    end
    return out
end

function rigs.rail(points: { DVec3 }, closed: boolean?, tension: number?)
    if #points < 2 then
        error("camera_rigs: a rail needs at least 2 points", 0)
    end
    local origin = points[1]
    local local_points = {}
    for i, p in points do
        local_points[i] = p - origin
    end
    local spline = curves.spline(local_points, { closed = closed, tension = tension })
    local arc = curves.spline_table(spline, 32)
    local samples = {}
    for i, u in arc.u do
        samples[i] = curves.spline_eval(spline, u)
    end
    return { origin = origin, spline = spline, arc = arc, samples = samples, length = arc.length, closed = closed == true }
end

function rigs.rail_clamp(rail, s: number): number
    if rail.closed then
        return rail.length > 0 and s % rail.length or 0
    end
    return math.clamp(s, 0, rail.length)
end

function rigs.rail_at(rail, s: number): (DVec3, DVec3)
    local u = curves.u_at_length(rail.arc, rigs.rail_clamp(rail, s))
    local tangent = dvec3.normalize(curves.spline_tangent(rail.spline, u))
    return rail.origin + curves.spline_eval(rail.spline, u), tangent
end

function rigs.rail_nearest(rail, point: DVec3): (number, number)
    local rel = point - rail.origin
    local samples, us = rail.samples, rail.arc.u
    local best, best_d = 1, math.huge
    for i, p in samples do
        local d = dvec3.length_squared(p - rel)
        if d < best_d then
            best, best_d = i, d
        end
    end
    local lo, hi = us[math.max(best - 1, 1)], us[math.min(best + 1, #us)]
    local spline = rail.spline
    local function dist(u: number): number
        return dvec3.length_squared(curves.spline_eval(spline, u) - rel)
    end
    local phi = 0.6180339887498949
    local a, b = lo, hi
    local c, d = b - (b - a) * phi, a + (b - a) * phi
    local fc, fd = dist(c), dist(d)
    for _ = 1, 24 do
        if fc < fd then
            b, d, fd = d, c, fc
            c = b - (b - a) * phi
            fc = dist(c)
        else
            a, c, fc = c, d, fd
            d = a + (b - a) * phi
            fd = dist(d)
        end
    end
    local u = (a + b) / 2
    return curves.length_at_u(rail.arc, u), math.sqrt(dist(u))
end

local lens_cache = {}

local function lens_keys(text: string)
    local cached = lens_cache[text]
    if cached ~= nil then
        return cached
    end
    local raw = {}
    for item in string.gmatch(text, "[^;]+") do
        local channel, t, v, mode = string.match(item, "^%s*(%a+)%s+(%S+)%s+(%S+)%s*(%S*)%s*$")
        local time, value = tonumber(t), tonumber(v)
        if channel == nil or time == nil or value == nil or (channel ~= "fov" and channel ~= "aperture" and channel ~= "exposure") then
            error("camera_rigs: bad lens key '" .. item .. "'", 0)
        end
        raw[channel] = raw[channel] or {}
        table.insert(raw[channel], { t = time, v = value, mode = mode ~= "" and mode or nil })
    end
    local tracks = {}
    for channel, keys in raw do
        tracks[channel] = curves.track(keys)
    end
    lens_cache[text] = tracks
    return tracks
end

rigs.lens_tracks = lens_keys

function rigs.lens(self, text: string, dt: number)
    if text == "" or text == nil then
        return
    end
    local clock = (self.lens_clock or 0) + dt
    self.lens_clock = clock
    local tracks = lens_keys(text)
    local entity = self.entity
    if tracks.fov then
        scene.set(entity, "Camera", "Fov", math.clamp(curves.track_eval(tracks.fov, clock), rigs.FOV_MIN, rigs.FOV_MAX))
    end
    if tracks.aperture then
        local value = curves.track_eval(tracks.aperture, clock)
        if value ~= self.lens_aperture then
            self.lens_aperture = value
            render.set("post.camera_aperture", value)
        end
    end
    if tracks.exposure then
        local value = curves.track_eval(tracks.exposure, clock)
        if value ~= self.lens_exposure then
            self.lens_exposure = value
            render.set("post.exposure", value)
        end
    end
end

return rigs
