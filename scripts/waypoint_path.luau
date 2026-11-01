local curves = require("lib/curves")
local dquat_lib = require("lib/dquat")

local ONCE, LOOP, PINGPONG = 0, 1, 2
local WRAPS = { [ONCE] = "clamp", [LOOP] = "loop", [PINGPONG] = "pingpong" }
local SAMPLES = 64

local function entries(text: string): { string }
    local out = {}
    for item in string.gmatch(text, "[^;]+") do
        local trimmed = string.match(item, "^%s*(.-)%s*$")
        if trimmed ~= "" then
            table.insert(out, trimmed)
        end
    end
    return out
end

local function numbers(text: string, count: number, what: string): ({ number }, string?)
    local tokens = {}
    for token in string.gmatch(text, "[^%s,]+") do
        table.insert(tokens, token)
    end
    if #tokens < count or #tokens > count + 1 then
        error("waypoint_path: bad " .. what .. " '" .. text .. "'", 0)
    end
    local values = {}
    for i = 1, count do
        values[i] = tonumber(tokens[i])
        if values[i] == nil then
            error("waypoint_path: bad " .. what .. " '" .. text .. "'", 0)
        end
    end
    return values, tokens[count + 1]
end

local function guid_hex(decimal: string): string
    local limbs = { 0, 0, 0 }
    for digit in string.gmatch(decimal, "%d") do
        local carry = tonumber(digit)
        for i = 1, 3 do
            local v = limbs[i] * 10 + carry
            limbs[i] = v % 16777216
            carry = v // 16777216
        end
    end
    return (string.gsub(string.format("%06x%06x%06x", limbs[3], limbs[2], limbs[1]), "^0+", ""))
end

local function resolve(name: string)
    local entity
    if string.match(name, "^%d+$") then
        local wanted = guid_hex(name)
        for _, candidate in scene.query("Transform") do
            local hex = string.match(tostring(candidate), "0x(%x+)")
            if hex ~= nil and string.gsub(hex, "^0+", "") == wanted then
                entity = candidate
                break
            end
        end
    else
        entity = scene.find(name)
    end
    if entity == nil then
        error("waypoint_path: unknown waypoint '" .. name .. "'", 0)
    end
    return entity
end

local function read_points(self, props)
    local out = {}
    if props.Waypoints ~= "" then
        local refs = self.refs
        if refs == nil or refs.text ~= props.Waypoints then
            refs = { text = props.Waypoints, list = {} }
            for _, name in entries(props.Waypoints) do
                table.insert(refs.list, resolve(name))
            end
            self.refs = refs
        end
        for _, entity in refs.list do
            if not scene.exists(entity) then
                self.refs = nil
                error("waypoint_path: a waypoint entity was destroyed", 0)
            end
            local x, y, z = scene.get_xyz(entity, "Transform", "Position")
            table.insert(out, { x, y, z })
        end
    else
        for _, item in entries(props.Points) do
            local values, extra = numbers(item, 3, "point")
            if extra ~= nil then
                error("waypoint_path: bad point '" .. item .. "'", 0)
            end
            table.insert(out, values)
        end
    end
    if #out < 2 then
        error("waypoint_path: needs at least 2 waypoints", 0)
    end
    return out
end

local function same(a, b): boolean
    if a == nil or #a ~= #b then
        return false
    end
    for i = 1, #b do
        local p, q = a[i], b[i]
        if p[1] ~= q[1] or p[2] ~= q[2] or p[3] ~= q[3] then
            return false
        end
    end
    return true
end

local function spec_of(props): string
    return table.concat({
        props.Waypoints, props.Points, tostring(props.Closed), tostring(props.Tension), tostring(props.Mode),
        tostring(props.Speed), tostring(props.Duration), props.Ease, props.TimeLaw, props.Orientation, props.FovKeys,
    }, "|")
end

local function keys_of(text: string, count: number, what: string, convert)
    local keys = {}
    for _, item in entries(text) do
        local time_text, rest = string.match(item, "^%s*(%S+)%s*(.*)$")
        local time = tonumber(time_text)
        if time == nil then
            error("waypoint_path: bad " .. what .. " key '" .. item .. "'", 0)
        end
        local values, mode = numbers(rest, count, what .. " key")
        table.insert(keys, { t = time, v = convert(values), mode = mode })
    end
    return keys
end

local function build(props, points)
    local origin = dvec3.new(points[1][1], points[1][2], points[1][3])
    local local_points = {}
    for i, p in points do
        local_points[i] = dvec3.new(p[1] - points[1][1], p[2] - points[1][2], p[3] - points[1][3])
    end
    local spline = curves.spline(local_points, { closed = props.Closed, tension = props.Tension })
    local segments = curves.spline_segments(spline)
    local arc = curves.spline_table(spline, SAMPLES)
    local length = arc.length
    local mode = props.Mode

    local law
    if props.TimeLaw ~= "" then
        law = curves.track(keys_of(props.TimeLaw, 1, "time law", function(v) return v[1] end), { wrap = "clamp" })
        local last = law.keys[#law.keys].v
        if last > segments + 1e-9 then
            error("waypoint_path: time law reaches waypoint " .. last .. " but the path has " .. segments .. " segments", 0)
        end
    end
    local orientation
    if props.Orientation ~= "" then
        orientation = curves.track(keys_of(props.Orientation, 3, "orientation", function(v)
            return dquat.from_euler(v[1], v[2], v[3])
        end), { wrap = WRAPS[mode] })
    end
    local fov
    if props.FovKeys ~= "" then
        fov = curves.track(keys_of(props.FovKeys, 1, "fov", function(v) return v[1] end), { wrap = WRAPS[mode] })
    end

    local first, span, speed = 0, 0, props.Speed
    if law then
        first = law.keys[1].t
        span = curves.track_duration(law)
    elseif props.Duration > 0 then
        span = props.Duration
    elseif speed > 0 then
        span = length / speed
    end
    local eased = props.Duration > 0 and props.Ease or "linear"

    local function pass(f: number)
        local u
        if law then
            u = curves.track_eval(law, first + f)
        else
            local fraction = span > 0 and curves.ease(eased, f / span) or 0
            u = curves.u_at_length(arc, length * fraction)
        end
        return origin + curves.spline_eval(spline, u), dvec3.normalize(curves.spline_tangent(spline, u))
    end

    return function(t: number)
        local running = math.max(t - first, 0)
        local index, f = 0, running
        if span > 0 then
            if mode == ONCE then
                f = math.min(running, span)
                index = running >= span and 1 or 0
            else
                index = math.floor(running / span)
                f = running - index * span
            end
        end
        local reverse = mode == PINGPONG and index % 2 == 1
        if reverse then
            f = span - f
        end
        local position, forward = pass(f)
        if reverse then
            forward = -forward
        end
        local rotation = orientation and curves.track_eval(orientation, t) or nil
        local field = fov and curves.track_eval(fov, t) or nil
        return position, forward, rotation, field, index
    end
end

return {
    properties = {
        { name = "Waypoints", type = "string", default = "" },
        { name = "Points", type = "string", default = "" },
        { name = "Closed", type = "bool", default = false },
        { name = "Tension", type = "float", default = 0, min = -1, max = 1 },
        { name = "Mode", type = "int", default = 0, min = 0, max = 2 },
        { name = "Speed", type = "float", unit = "m/s", default = 1000, min = 0, max = 1e15 },
        { name = "Duration", type = "float", unit = "s", default = 0, min = 0, max = 1e9 },
        { name = "Ease", type = "string", default = "linear" },
        { name = "TimeLaw", type = "string", default = "" },
        { name = "StartTime", type = "float", unit = "s", default = 0, min = -1e9, max = 1e9 },
        { name = "Target", type = "entity" },
        { name = "FaceTravel", type = "bool", default = false },
        { name = "Orientation", type = "string", default = "" },
        { name = "FovKeys", type = "string", default = "" },
        { name = "MakePrimary", type = "bool", default = false },
        { name = "Name", type = "string", default = "" },
    },

    OnStart = function(self: ScriptSelf)
        self.lap = 0
        self.done = false
        if self.props.MakePrimary and scene.get(self.entity, "Camera", "Primary") ~= nil then
            scene.set(self.entity, "Camera", "Primary", true)
        end
    end,

    OnTick = function(self: ScriptSelf, dt: number)
        local props = self.props
        local entity = self.entity
        local points = read_points(self, props)
        local spec = spec_of(props)
        if self.evaluate == nil or self.spec ~= spec or not same(self.points, points) then
            self.evaluate = build(props, points)
            self.spec = spec
            self.points = points
        end

        local t = math.max(time.elapsed() - props.StartTime, 0)
        local position, forward, rotation, field, index = self.evaluate(t)
        scene.set64(entity, "Transform", "Position", position)

        local target = props.Target
        if rotation == nil and target ~= nil and scene.exists(target) then
            rotation = dquat_lib.look_rotation(scene.get64(target, "Transform", "Position") - position)
        elseif rotation == nil and props.FaceTravel then
            rotation = dquat_lib.look_rotation(forward)
        end
        if rotation ~= nil then
            scene.set64(entity, "Transform", "Rotation", rotation)
        end
        if field ~= nil then
            scene.set(entity, "Camera", "Fov", field)
        end

        local name = props.Name ~= "" and props.Name or scene.name(entity)
        if props.Mode == ONCE then
            local finished = index > 0
            if finished and not self.done then
                events.emit("waypoint_path_done", name)
            end
            self.done = finished
        else
            if index > self.lap then
                events.emit("waypoint_path_lap", name, index)
            end
            self.lap = index
        end
    end,

    OnStop = function(self: ScriptSelf)
        self.evaluate = nil
    end,
}
