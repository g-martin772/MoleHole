local curves = require("lib/curves")
local dquat_lib = require("lib/dquat")

local dvec = {}

dvec.new = dvec3.new
dvec.from_vec = dvec3.from_vec
dvec.to_vec = dvec3.to_vec
dvec.dot = dvec3.dot
dvec.cross = dvec3.cross
dvec.length = dvec3.length
dvec.normalize = dvec3.normalize
dvec.distance = dvec3.distance
dvec.lerp = dvec3.lerp
dvec.damp = curves.damp
dvec.smooth_damp = curves.smooth_damp

dvec.ZERO = dvec3.new(0, 0, 0)
dvec.ONE = dvec3.new(1, 1, 1)
dvec.X = dvec3.new(1, 0, 0)
dvec.Y = dvec3.new(0, 1, 0)
dvec.Z = dvec3.new(0, 0, 1)

function dvec.read(entity: Entity, component: string, field: string): DVec3
    return scene.get64(entity, component, field)
end

function dvec.from_table(t: { x: number, y: number, z: number }): DVec3
    return dvec3.new(t.x, t.y, t.z)
end

function dvec.to_table(v: DVec3): { x: number, y: number, z: number }
    return { x = v.x, y = v.y, z = v.z }
end

function dvec.sum(list: { DVec3 }): DVec3
    local total = dvec.ZERO
    for _, v in list do
        total = total + v
    end
    return total
end

function dvec.angle(a: DVec3, b: DVec3): number
    local denominator = dvec3.length(a) * dvec3.length(b)
    if denominator == 0 then
        return 0
    end
    return math.acos(math.clamp(dvec3.dot(a, b) / denominator, -1, 1))
end

function dvec.project(v: DVec3, onto: DVec3): DVec3
    local d = dvec3.length_squared(onto)
    if d == 0 then
        return dvec.ZERO
    end
    return onto * (dvec3.dot(v, onto) / d)
end

function dvec.reflect(v: DVec3, normal: DVec3): DVec3
    local n = dvec3.normalize(normal)
    return v - n * (2 * dvec3.dot(v, n))
end

function dvec.clamp_length(v: DVec3, max: number): DVec3
    local length = dvec3.length(v)
    if length <= max or length == 0 then
        return v
    end
    return v * (max / length)
end

function dvec.move_towards(from: DVec3, to: DVec3, max_delta: number): DVec3
    local delta = to - from
    local length = dvec3.length(delta)
    if length <= max_delta or length == 0 then
        return to
    end
    return from + delta * (max_delta / length)
end

function dvec.near(a: DVec3, b: DVec3, epsilon: number): boolean
    return dvec3.distance(a, b) <= epsilon
end

function dvec.set_length(v: DVec3, length: number): DVec3
    return dvec3.normalize(v) * length
end

function dvec.perpendicular(v: DVec3): DVec3
    local axis = math.abs(v.x) <= math.abs(v.y) and math.abs(v.x) <= math.abs(v.z) and dvec.X
        or math.abs(v.y) <= math.abs(v.z) and dvec.Y
        or dvec.Z
    return dvec3.normalize(dvec3.cross(v, axis))
end

function dvec.signed_angle(a: DVec3, b: DVec3, axis: DVec3): number
    local angle = dvec.angle(a, b)
    return dvec3.dot(dvec3.cross(a, b), axis) < 0 and -angle or angle
end

function dvec.rotate_towards(from: DVec3, to: DVec3, max_radians: number, max_length_delta: number): DVec3
    local from_length, to_length = dvec3.length(from), dvec3.length(to)
    local length = from_length + math.clamp(to_length - from_length, -max_length_delta, max_length_delta)
    if from_length == 0 then
        return to_length == 0 and from or dvec3.normalize(to) * length
    elseif to_length == 0 then
        return from * (length / from_length)
    end
    local angle = dvec.angle(from, to)
    local step = angle <= max_radians and 1 or max_radians / angle
    local turn = dquat.slerp(dquat_lib.IDENTITY, dquat_lib.from_to(from, to), step)
    return dquat.rotate(turn, from) * (length / from_length)
end

return dvec
