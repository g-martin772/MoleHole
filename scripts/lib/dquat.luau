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
    return 2 * math.acos(math.clamp(math.abs(dquat.dot(a, b)), 0, 1))
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

return dquat_lib
