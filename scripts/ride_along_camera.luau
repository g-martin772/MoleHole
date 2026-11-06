local camera_cycle = require("lib/camera_cycle")
local dquat_lib = require("lib/dquat")
local rigs = require("lib/camera_rigs")

return {
    properties = {
        { name = "Target", type = "entity" },
        { name = "Next", type = "entity" },
        { name = "Focus", type = "entity" },
        { name = "Offset", type = "vec3" },
        { name = "Pitch", type = "float", default = 0, min = -180, max = 180 },
        { name = "Yaw", type = "float", default = 0, min = -180, max = 180 },
        { name = "Roll", type = "float", default = 0, min = -180, max = 180 },
        { name = "LeadTime", type = "float", unit = "s", default = 0, min = 0, max = 3600 },
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
        if not camera_cycle.step(self, "ride", props.Next) then
            return
        end
        local target = props.Target
        if target == nil or not scene.exists(target) then
            return
        end
        rigs.forget(self)
        local anchor = scene.get64(target, "Transform", "Position")
        local rotation = scene.get64(target, "Transform", "Rotation")
        local look = dquat.from_euler(props.Pitch, props.Yaw, props.Roll)
        local position, view = rigs.ride_along(anchor, rotation, dvec3.from_vec(props.Offset), look)
        local focus = props.Focus
        if focus ~= nil and scene.exists(focus) then
            local velocity = scene.get64(focus, "Velocity", "Linear") or dvec3.new(0, 0, 0)
            view = rigs.look_at_lead(position, scene.get64(focus, "Transform", "Position"), velocity, props.LeadTime, dquat_lib.up(rotation))
        end
        scene.set64(self.entity, "Transform", "Position", position)
        scene.set64(self.entity, "Transform", "Rotation", view)
        rigs.lens(self, props.LensKeys, dt)
        rigs.effects(self, props, dt, position, view)
    end,
}
