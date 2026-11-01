local curves = require("lib/curves")

local dquat_lib = {}

dquat_lib.new = dquat.new
dquat_lib.from_axis_angle = dquat.from_axis_angle
dquat_lib.from_euler = dquat.from_euler
dquat_lib.mul = dquat.mul
dquat_lib.rotate = dquat.rotate
dquat_lib.slerp = dquat.slerp
dquat_lib.inverse = dquat.inverse
dquat_lib.IDENTITY = dquat.new(0, 0, 0, 1)

function dquat_lib.from_to(from: DVec3, to: DVec3): DQuat
    local a, b = dvec3.normalize(from), dvec3.normalize(to)
    local d = dvec3.dot(a, b)
    if d > 1 - 1e-15 then
        return dquat_lib.IDENTITY
    end
    if d < -1 + 1e-15 then
        local axis = dvec3.cross(dvec3.new(1, 0, 0), a)
        if dvec3.length_squared(axis) < 1e-6 then
            axis = dvec3.cross(dvec3.new(0, 1, 0), a)
        end
        return dquat.from_axis_angle(dvec3.normalize(axis), math.pi)
    end
    local c = dvec3.cross(a, b)
    return dquat.normalize(dquat.new(c.x, c.y, c.z, 1 + d))
end

function dquat_lib.look_rotation(forward: DVec3, up: DVec3?): DQuat
    local f = dvec3.normalize(forward)
    if dvec3.length_squared(f) == 0 then
        return dquat_lib.IDENTITY
    end
    local hint = up or dvec3.new(0, 1, 0)
    if math.abs(dvec3.dot(f, dvec3.normalize(hint))) > 0.9999999999 then
        hint = dvec3.new(0, 0, 1)
    end
    local z = -f
    local x = dvec3.normalize(dvec3.cross(hint, z))
    local y = dvec3.cross(z, x)
    local trace = x.x + y.y + z.z
    if trace > 0 then
        local s = 0.5 / math.sqrt(trace + 1)
        return dquat.new((y.z - z.y) * s, (z.x - x.z) * s, (x.y - y.x) * s, 0.25 / s)
    elseif x.x > y.y and x.x > z.z then
        local s = 2 * math.sqrt(1 + x.x - y.y - z.z)
        return dquat.new(0.25 * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s)
    elseif y.y > z.z then
        local s = 2 * math.sqrt(1 + y.y - x.x - z.z)
        return dquat.new((y.x + x.y) / s, 0.25 * s, (z.y + y.z) / s, (z.x - x.z) / s)
    end
    local s = 2 * math.sqrt(1 + z.z - x.x - y.y)
    return dquat.new((z.x + x.z) / s, (z.y + y.z) / s, 0.25 * s, (x.y - y.x) / s)
end

function dquat_lib.angle(a: DQuat, b: DQuat): number
    local d = dquat.mul(b, dquat.inverse(a))
    return 2 * math.atan2(math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z), math.abs(d.w))
end

function dquat_lib.rotate_towards(from: DQuat, to: DQuat, max_radians: number): DQuat
    local angle = dquat_lib.angle(from, to)
    if angle <= max_radians or angle == 0 then
        return to
    end
    return dquat.slerp(from, to, max_radians / angle)
end

function dquat_lib.forward(q: DQuat): DVec3
    return dquat.rotate(q, dvec3.new(0, 0, -1))
end

function dquat_lib.up(q: DQuat): DVec3
    return dquat.rotate(q, dvec3.new(0, 1, 0))
end

function dquat_lib.right(q: DQuat): DVec3
    return dquat.rotate(q, dvec3.new(1, 0, 0))
end

function dquat_lib.to_rotation_vector(q: DQuat): DVec3
    local n = dquat.normalize(q)
    local v = dvec3.new(n.x, n.y, n.z)
    if n.w < 0 then
        v = -v
        n = dquat.new(-n.x, -n.y, -n.z, -n.w)
    end
    local s = dvec3.length(v)
    if s < 1e-12 then
        return v * (2 / n.w)
    end
    return v * (2 * math.atan2(s, n.w) / s)
end

function dquat_lib.from_rotation_vector(v: DVec3): DQuat
    local angle = dvec3.length(v)
    if angle < 1e-15 then
        return dquat.normalize(dquat.new(v.x / 2, v.y / 2, v.z / 2, 1))
    end
    return dquat.from_axis_angle(v / angle, angle)
end

function dquat_lib.delta(from: DQuat, to: DQuat): DVec3
    return dquat_lib.to_rotation_vector(dquat.mul(to, dquat.inverse(from)))
end

function dquat_lib.integrate(q: DQuat, angular_velocity: DVec3, dt: number): DQuat
    return dquat.normalize(dquat.mul(dquat_lib.from_rotation_vector(angular_velocity * dt), q))
end

function dquat_lib.look_at(from: DVec3, target: DVec3, up: DVec3?): DQuat
    return dquat_lib.look_rotation(target - from, up)
end

function dquat_lib.damp(current: DQuat, target: DQuat, lambda: number, dt: number): DQuat
    return dquat.slerp(current, target, 1 - math.exp(-lambda * dt))
end

function dquat_lib.smooth_damp(current: DQuat, target: DQuat, angular_velocity: DVec3, smooth_time: number, dt: number, max_speed: number?): (DQuat, DVec3)
    local step, velocity = curves.smooth_damp(dvec3.new(0, 0, 0), dquat_lib.delta(current, target), angular_velocity, smooth_time, dt, max_speed)
    return dquat.mul(dquat_lib.from_rotation_vector(step), current), velocity
end

return dquat_lib
