local camera_cycle = require("lib/camera_cycle")
local rigs = require("lib/camera_rigs")
local zoom = require("lib/zoom")

return {
    properties = {
        { name = "Target", type = "entity" },
        { name = "Next", type = "entity" },
        { name = "Distance", type = "float", unit = "m", default = 300, min = 0, max = 1e12 },
        { name = "Height", type = "float", unit = "m", default = 80, min = -1e12, max = 1e12 },
        { name = "LookAhead", type = "float", unit = "m", default = 400, min = 0, max = 1e12 },
        { name = "SmoothTime", type = "float", unit = "s", default = 0.25, min = 0, max = 60 },
        { name = "TurnTime", type = "float", unit = "s", default = 0.15, min = 0, max = 60 },
        { name = "ZoomMin", type = "float", default = 0.1, min = 1e-6, max = 1e6 },
        { name = "ZoomMax", type = "float", default = 10, min = 1e-6, max = 1e6 },
        { name = "ZoomSpeed", type = "float", default = 0.1, min = 0, max = 10 },
        { name = "ZoomSmooth", type = "float", unit = "s", default = 0.12, min = 0, max = 60 },
        { name = "Seed", type = "int", default = 0, min = 0, max = 1073741824 },
        { name = "ShakeAngle", type = "float", default = 2, min = 0, max = 90 },
        { name = "ShakeOffset", type = "float", unit = "m", default = 0, min = 0, max = 1e9 },
        { name = "ShakeDecay", type = "float", default = 1.5, min = 0, max = 100 },
        { name = "Handheld", type = "float", default = 0, min = 0, max = 45 },
        { name = "LensKeys", type = "string", default = "" },
    },

    OnStart = function(self: ScriptSelf)
        self.vel = vector.zero
        self.turn = vector.zero
    end,

    OnEvent = function(self: ScriptSelf, name: string, a: any)
        rigs.on_shake(self, name, a)
    end,

    OnLateTick = function(self: ScriptSelf, dt: number)
        local props = self.props
        if not camera_cycle.step(self, "chase", props.Next) then
            return
        end
        local target = props.Target
        if target == nil or not scene.exists(target) then
            return
        end
        local anchor = scene.get64(target, "Transform", "Position")
        local rotation = scene.get64(target, "Transform", "Rotation")
        local scale = zoom.step(self, props, dt)
        local rel, position = rigs.follow(self, anchor, rotation, props.Height * scale, props.Distance * scale, props.SmoothTime, dt)
        local view = rigs.chase_aim(self, rotation, rel, props.LookAhead, props.TurnTime, dt)
        rigs.lens(self, props.LensKeys, dt)
        rigs.effects(self, props, dt, position, view)
    end,
}
