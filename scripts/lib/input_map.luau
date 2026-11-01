local input_map = {}

local function clamp_deadzone(deadzone: number?): number
    return math.clamp(deadzone or 0, 0, 0.999)
end

local function curve(magnitude: number, deadzone: number, expo: number): number
    if magnitude <= deadzone then
        return 0
    end
    local scaled = math.min(1, (magnitude - deadzone) / (1 - deadzone))
    return (1 - expo) * scaled + expo * scaled * scaled * scaled
end

function input_map.shape(x: number, options: { deadzone: number?, expo: number?, sensitivity: number?, invert: boolean? }?): number
    local opts = options or {}
    local shaped = curve(math.abs(x), clamp_deadzone(opts.deadzone), opts.expo or 0)
    if shaped == 0 then
        return 0
    end
    local out = math.sign(x) * shaped * (opts.sensitivity or 1)
    return opts.invert and -out or out
end

function input_map.stick_shape(x: number, y: number, options: { deadzone: number?, expo: number?, sensitivity: number?, invert_y: boolean? }?): (number, number)
    local opts = options or {}
    local length = math.sqrt(x * x + y * y)
    if length == 0 then
        return 0, 0
    end
    if length > 1 then
        x, y, length = x / length, y / length, 1
    end
    local k = curve(length, clamp_deadzone(opts.deadzone), opts.expo or 0) * (opts.sensitivity or 1) / length
    return x * k, opts.invert_y and -y * k or y * k
end

function input_map.new(definition: { axes: { [string]: any }?, toggles: { [string]: any }?, repeats: { [string]: any }?, sticks: { [string]: any }? })
    local state = {
        axes = definition.axes or {},
        toggles = definition.toggles or {},
        repeats = definition.repeats or {},
        sticks = definition.sticks or {},
        axis = {},
        toggle = {},
        repeat_count = {},
        repeat_clock = {},
        stick = {},
    }
    for name in state.axes do
        state.axis[name] = 0
    end
    for name, def in state.toggles do
        state.toggle[name] = def.initial == true
    end
    for name in state.repeats do
        state.repeat_count[name] = 0
        state.repeat_clock[name] = { held = 0, next = 0 }
    end
    for name in state.sticks do
        state.stick[name] = { x = 0, y = 0 }
    end
    return state
end

local function raw_axis(def, source): number
    if def.action then
        return source.axis(def.action)
    end
    local value = 0
    if def.positive then
        value += source.axis(def.positive)
    end
    if def.negative then
        value -= source.axis(def.negative)
    end
    return math.clamp(value, -1, 1)
end

function input_map.update(state, dt: number, source: any?)
    local src = source or input
    for name, def in state.axes do
        local target = input_map.shape(raw_axis(def, src), def)
        if def.slew then
            local limit = def.slew * dt
            target = state.axis[name] + math.clamp(target - state.axis[name], -limit, limit)
        end
        state.axis[name] = target
    end
    for name, def in state.sticks do
        local value = state.stick[name]
        value.x, value.y = input_map.stick_shape(src.axis(def.x), src.axis(def.y), def)
    end
    for name, def in state.toggles do
        if src.pressed(def.action) then
            state.toggle[name] = not state.toggle[name]
        end
    end
    for name, def in state.repeats do
        local clock = state.repeat_clock[name]
        local count = 0
        if src.pressed(def.action) then
            clock.held, clock.next = 0, def.delay or 0.4
            count = 1
        elseif src.held(def.action) then
            clock.held += dt
            local interval = 1 / (def.rate or 10)
            while clock.held >= clock.next and count < 64 do
                count += 1
                clock.next += interval
            end
        else
            clock.held, clock.next = 0, 0
        end
        state.repeat_count[name] = count
    end
end

local function lookup(map, name: string, kind: string)
    local value = map[name]
    if value == nil then
        error("input_map: unknown " .. kind .. " '" .. tostring(name) .. "'", 3)
    end
    return value
end

function input_map.axis(state, name: string): number
    return lookup(state.axis, name, "axis")
end

function input_map.stick(state, name: string): (number, number)
    local value = lookup(state.stick, name, "stick")
    return value.x, value.y
end

function input_map.toggled(state, name: string): boolean
    return lookup(state.toggle, name, "toggle")
end

function input_map.set_toggle(state, name: string, value: boolean)
    lookup(state.toggle, name, "toggle")
    state.toggle[name] = value
end

function input_map.repeats(state, name: string): number
    return lookup(state.repeat_count, name, "repeat")
end

return input_map
