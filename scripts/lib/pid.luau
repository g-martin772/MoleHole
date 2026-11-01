local pid = {}

type Gains = {
    kp: number?,
    ki: number?,
    kd: number?,
    out_min: number?,
    out_max: number?,
    i_limit: number?,
    d_tau: number?,
}

local function new_state(gains: Gains, channels: number)
    local state = {
        kp = gains.kp or 0,
        ki = gains.ki or 0,
        kd = gains.kd or 0,
        out_min = gains.out_min or -math.huge,
        out_max = gains.out_max or math.huge,
        i_limit = gains.i_limit or math.huge,
        d_tau = gains.d_tau or 0,
        channels = channels,
        primed = false,
        integral = {},
        previous = {},
        derivative = {},
        output = {},
    }
    for i = 1, channels do
        state.integral[i] = 0
        state.previous[i] = 0
        state.derivative[i] = 0
        state.output[i] = 0
    end
    return state
end

function pid.new(gains: Gains)
    return new_state(gains, 1)
end

function pid.new_vec(gains: Gains)
    return new_state(gains, 3)
end

function pid.reset(state)
    state.primed = false
    for i = 1, state.channels do
        state.integral[i] = 0
        state.previous[i] = 0
        state.derivative[i] = 0
        state.output[i] = 0
    end
end

local function channel(state, i: number, setpoint: number, measurement: number, rate: number?, dt: number): number
    local err = setpoint - measurement
    local raw = 0
    if rate then
        raw = -rate
    elseif state.primed then
        raw = -(measurement - state.previous[i]) / dt
    end
    local d = raw
    if state.d_tau > 0 then
        d = state.derivative[i] + (raw - state.derivative[i]) * (dt / (state.d_tau + dt))
    end
    state.derivative[i] = d
    state.previous[i] = measurement
    local old = state.integral[i]
    local integral = math.clamp(old + state.ki * err * dt, -state.i_limit, state.i_limit)
    local pd = state.kp * err + state.kd * d
    local output = math.clamp(pd + integral, state.out_min, state.out_max)
    if pd + integral > state.out_max and err > 0 then
        integral = math.max(old, math.min(integral, state.out_max - pd))
    elseif pd + integral < state.out_min and err < 0 then
        integral = math.min(old, math.max(integral, state.out_min - pd))
    end
    state.integral[i] = integral
    state.output[i] = output
    return output
end

function pid.step(state, setpoint: number, measurement: number, dt: number, rate: number?): number
    if dt <= 0 then
        return state.output[1]
    end
    local out = channel(state, 1, setpoint, measurement, rate, dt)
    state.primed = true
    return out
end

local function pack(wide: boolean, x: number, y: number, z: number)
    if wide then
        return dvec3.new(x, y, z)
    end
    return vector.create(x, y, z)
end

function pid.step_vec(state, setpoint, measurement, dt: number, rate)
    local wide = typeof(measurement) == "dvec3"
    if dt <= 0 then
        return pack(wide, state.output[1], state.output[2], state.output[3])
    end
    local out = {}
    for i, axis in { "x", "y", "z" } do
        out[i] = channel(state, i, setpoint[axis], measurement[axis], rate and rate[axis], dt)
    end
    state.primed = true
    return pack(wide, out[1], out[2], out[3])
end

return pid
