local camera_cycle = require("lib/camera_cycle")
local curves = require("lib/curves")
local dquat_lib = require("lib/dquat")
local rigs = require("lib/camera_rigs")

local cache = {}

local function rail_of(self, props)
    local key = props.Points .. "|" .. tostring(props.Closed) .. "|" .. tostring(props.Tension)
    local entry = cache[self.entity]
    if entry == nil or entry.key ~= key then
        entry = { key = key, rail = rigs.rail(rigs.parse_points(props.Points), props.Closed, props.Tension) }
        cache[self.entity] = entry
    end
    return entry.rail
end

return {
    properties = {
        { name = "Points", type = "string", default = "" },
        { name = "Closed", type = "bool", default = false },
        { name = "Tension", type = "float", default = 0, min = -1, max = 1 },
        { name = "Target", type = "entity" },
        { name = "Next", type = "entity" },
        { name = "Follow", type = "bool", default = true },
        { name = "Speed", type = "float", unit = "m/s", default = 10, min = 0, max = 1e12 },
        { name = "Start", type = "float", unit = "m", default = 0, min = 0, max = 1e15 },
        { name = "SmoothTime", type = "float", unit = "s", default = 0.5, min = 0, max = 60 },
        { name = "LookLead", type = "float", unit = "s", default = 0, min = 0, max = 3600 },
        { name = "Seed", type = "int", default = 0, min = 0, max = 1073741824 },
        { name = "ShakeAngle", type = "float", default = 2, min = 0, max = 90 },
        { name = "ShakeOffset", type = "float", unit = "m", default = 0, min = 0, max = 1e9 },
        { name = "ShakeDecay", type = "float", default = 1.5, min = 0, max = 100 },
        { name = "Handheld", type = "float", default = 0, min = 0, max = 45 },
        { name = "LensKeys", type = "string", default = "" },
    },

    OnEvent = function(self: ScriptSelf, name: string, a: any)
        rigs.on_shake(self, name, a)
    end,

    OnLateTick = function(self: ScriptSelf, dt: number)
        local props = self.props
        if not camera_cycle.step(self, "rail", props.Next) then
            return
        end
        local rail = rail_of(self, props)
        local target = props.Target
        local has_target = target ~= nil and scene.exists(target)
        local focus = has_target and scene.get64(target, "Transform", "Position") or nil
        rigs.forget(self)

        if props.Follow and focus ~= nil then
            local want = rigs.rail_nearest(rail, focus)
            if self.s == nil then
                self.s, self.s_vel = want, 0
            else
                if rail.closed and rail.length > 0 then
                    want = self.s + (want - self.s + rail.length / 2) % rail.length - rail.length / 2
                end
                self.s, self.s_vel = curves.smooth_damp(self.s, want, self.s_vel or 0, props.SmoothTime, dt)
            end
        else
            self.s = rigs.rail_clamp(rail, (self.s or props.Start) + props.Speed * dt)
        end

        local position, tangent = rigs.rail_at(rail, self.s)
        local rotation
        if focus ~= nil then
            local velocity = scene.get64(target, "Velocity", "Linear") or dvec3.new(0, 0, 0)
            rotation = rigs.look_at_lead(position, focus, velocity, props.LookLead)
        else
            rotation = dquat_lib.look_rotation(tangent)
        end
        scene.set64(self.entity, "Transform", "Position", position)
        scene.set64(self.entity, "Transform", "Rotation", rotation)
        rigs.lens(self, props.LensKeys, dt)
        rigs.effects(self, props, dt, position, rotation)
    end,

    OnStop = function(self: ScriptSelf)
        cache[self.entity] = nil
    end,
}
