local dquat_lib = require("lib/dquat")
local pid = require("lib/pid")

local flight = {}

flight.G = 9.80665
flight.OFF, flight.RATE, flight.HOLD, flight.RETRO = 0, 1, 2, 3

local AXES = { "x", "y", "z" }
local ALIGN = math.rad(8)
local SETTLED = 0.05

local DEFAULTS = {
    thrust_x = 1e4,
    thrust_y = 1e4,
    thrust_z = 2e4,
    torque_x = 2e4,
    torque_y = 2e4,
    torque_z = 2e4,
    inertia_x = 0,
    inertia_y = 0,
    inertia_z = 0,
    max_rate = math.rad(90),
    response = 0.4,
    mass = 0,
    g_limit = 0,
    boost = 2,
    retro_stop = 0.5,
    assist = flight.RATE,
}

local function clamp1(x: number?): number
    return math.clamp(x or 0, -1, 1)
end

function flight.new(config: { [string]: number }?)
    local state = {
        hold = { v_set = false, vx = 0, vy = 0, vz = 0, q_set = false, qx = 0, qy = 0, qz = 0, qw = 1 },
        rate_pid = {},
        last_assist = -1,
    }
    for key, value in DEFAULTS do
        state[key] = value
    end
    for _, axis in AXES do
        state.rate_pid[axis] = pid.new({ kp = 1, ki = 0, out_min = -1, out_max = 1, i_limit = 1 })
    end
    flight.configure(state, config)
    return state
end

function flight.configure(state, config: { [string]: number }?)
    if config == nil then
        return
    end
    for key, value in config do
        if DEFAULTS[key] == nil then
            error("flight.configure: unknown key '" .. tostring(key) .. "'", 2)
        end
        state[key] = value
    end
end

function flight.box_inertia(mass: number, hx: number, hy: number, hz: number): (number, number, number)
    local k = mass / 3
    return k * (hy * hy + hz * hz), k * (hx * hx + hz * hz), k * (hx * hx + hy * hy)
end

function flight.sphere_inertia(mass: number, radius: number): (number, number, number)
    local i = 0.4 * mass * radius * radius
    return i, i, i
end

function flight.collider_inertia(entity: Entity, mass: number): (number, number, number)
    local shape = scene.get(entity, "Collider", "Shape")
    local radius = scene.get(entity, "Collider", "Radius")
    if shape == 1 or shape == "Sphere" then
        return flight.sphere_inertia(mass, radius)
    end
    if shape == 2 or shape == "Capsule" then
        local half = scene.get(entity, "Collider", "HalfHeight")
        return flight.box_inertia(mass, half + radius, radius, radius)
    end
    local half = scene.get64(entity, "Collider", "HalfExtents")
    return flight.box_inertia(mass, half.x, half.y, half.z)
end

function flight.read(entity: Entity)
    return {
        mass = math.clamp(scene.get(entity, "RigidBody", "Mass"), 1e-4, 1e37),
        rotation = scene.get64(entity, "Transform", "Rotation"),
        velocity = scene.get64(entity, "Velocity", "Linear") or dvec3.new(0, 0, 0),
        angular_velocity = scene.get64(entity, "Velocity", "Angular") or dvec3.new(0, 0, 0),
    }
end

local function body_vector(q, v)
    return dquat.rotate(dquat.inverse(q), v)
end

local function retarget(state, assist: number)
    if state.last_assist == assist then
        return
    end
    state.last_assist = assist
    local hold = state.hold
    hold.v_set, hold.q_set = false, false
    for _, axis in AXES do
        pid.reset(state.rate_pid[axis])
    end
end

local function attitude_rates(state, q, target, max_rate: number)
    local error_body = body_vector(q, dquat_lib.delta(q, target))
    local rates = {}
    for i, axis in AXES do
        rates[i] = math.clamp(error_body[axis] / state.response, -max_rate, max_rate)
    end
    return rates
end

local function rate_targets(state, body, command, wb, rotating: boolean, assist: number)
    local max_rate = state.max_rate
    local zero = { 0, 0, 0 }
    if rotating then
        state.hold.q_set = false
        return { clamp1(command.pitch) * max_rate, -clamp1(command.yaw) * max_rate, -clamp1(command.roll) * max_rate }
    elseif assist < flight.HOLD then
        return zero
    end
    local hold = state.hold
    local q = body.rotation
    if assist == flight.RETRO then
        local speed = dvec3.length(body.velocity)
        if speed > state.retro_stop then
            local up = dquat.rotate(q, dvec3.new(0, 1, 0))
            return attitude_rates(state, q, dquat_lib.look_rotation(-body.velocity, up), max_rate)
        end
    end
    if not hold.q_set then
        if math.max(math.abs(wb.x), math.abs(wb.y), math.abs(wb.z)) > SETTLED * max_rate then
            return zero
        end
        hold.q_set, hold.qx, hold.qy, hold.qz, hold.qw = true, q.x, q.y, q.z, q.w
        return zero
    end
    return attitude_rates(state, q, dquat.new(hold.qx, hold.qy, hold.qz, hold.qw), max_rate)
end

local function torques(state, targets, wb, dt: number, mass: number, body_mass: number)
    local limits = { state.torque_x, state.torque_y, state.torque_z }
    local out = {}
    for i, axis in AXES do
        local inertia = state["inertia_" .. axis]
        if inertia <= 0 then
            inertia = mass
        else
            inertia *= mass / body_mass
        end
        local p = state.rate_pid[axis]
        p.kp = inertia / state.response
        p.ki = p.kp / (4 * state.response)
        p.out_min, p.out_max, p.i_limit = -limits[i], limits[i], limits[i]
        out[i] = pid.step(p, targets[i], wb[axis], dt)
    end
    return out
end

local function manual_torques(state, command)
    return {
        clamp1(command.pitch) * state.torque_x,
        -clamp1(command.yaw) * state.torque_y,
        -clamp1(command.roll) * state.torque_z,
    }
end

local function hold_force(state, body, qi, mass: number, dt: number, limits)
    local hold = state.hold
    local v = body.velocity
    if not hold.v_set then
        hold.v_set, hold.vx, hold.vy, hold.vz = true, v.x, v.y, v.z
    end
    local wanted = dvec3.new(hold.vx, hold.vy, hold.vz) - v
    local force = wanted * (mass / math.max(dt, state.response))
    local local_force = dquat.rotate(qi, force)
    return {
        math.clamp(local_force.x, -limits[1], limits[1]),
        math.clamp(local_force.y, -limits[2], limits[2]),
        math.clamp(local_force.z, -limits[3], limits[3]),
    }
end

function flight.step(state, body, command, dt: number)
    local assist = math.clamp(math.floor(state.assist), flight.OFF, flight.RETRO)
    retarget(state, assist)
    local mass = state.mass > 0 and state.mass or body.mass
    local scale = body.mass / mass
    local q = body.rotation
    local qi = dquat.inverse(q)
    local wb = body_vector(q, body.angular_velocity)
    local hold = state.hold

    local rotating = math.abs(clamp1(command.pitch)) + math.abs(clamp1(command.yaw)) + math.abs(clamp1(command.roll)) > 1e-4
    local torque_body
    if assist == flight.OFF then
        torque_body = manual_torques(state, command)
    else
        torque_body = torques(state, rate_targets(state, body, command, wb, rotating, assist), wb, dt, mass, body.mass)
    end

    local boost = command.boost and state.boost or 1
    local limits = { state.thrust_x * boost, state.thrust_y * boost, state.thrust_z * boost }
    local cx, cy, cz = clamp1(command.x), clamp1(command.y), clamp1(command.z)
    local translating = math.abs(cx) + math.abs(cy) + math.abs(cz) > 1e-4
    local force_body
    if translating then
        hold.v_set = false
        force_body = { cx * limits[1], cy * limits[2], -cz * limits[3] }
    elseif assist >= flight.HOLD then
        force_body = { 0, 0, 0 }
        local braking = false
        if assist == flight.RETRO then
            local speed = dvec3.length(body.velocity)
            if speed > state.retro_stop then
                braking = true
                local forward = dquat_lib.forward(q)
                if dvec3.dot(forward, -body.velocity) / speed > math.cos(ALIGN) then
                    local share = math.min(1, speed * mass / (state.thrust_z * dt))
                    force_body[3] = -state.thrust_z * share
                end
            else
                hold.v_set, hold.vx, hold.vy, hold.vz = true, 0, 0, 0
            end
        end
        if not braking then
            force_body = hold_force(state, body, qi, mass, dt, limits)
        end
    else
        force_body = { 0, 0, 0 }
    end

    local accel = math.sqrt(force_body[1] ^ 2 + force_body[2] ^ 2 + force_body[3] ^ 2) / mass
    local limit = state.g_limit * flight.G
    local clamped = false
    if limit > 0 and accel > limit then
        local k = limit / accel
        force_body = { force_body[1] * k, force_body[2] * k, force_body[3] * k }
        accel, clamped = limit, true
    end

    local force = dquat.rotate(q, dvec3.new(force_body[1], force_body[2], force_body[3])) * scale
    local torque = dquat.rotate(q, dvec3.new(torque_body[1], torque_body[2], torque_body[3])) * scale
    return force, torque, { accel = accel, g = accel / flight.G, clamped = clamped, assist = assist, boosted = command.boost == true and boost > 1 }
end

function flight.apply(entity: Entity, state, command, dt: number)
    local force, torque, info = flight.step(state, flight.read(entity), command, dt)
    if dvec3.length_squared(force) > 0 then
        physics.add_force(entity, force)
    end
    if dvec3.length_squared(torque) > 0 then
        physics.add_torque(entity, torque)
    end
    return info
end

return flight
