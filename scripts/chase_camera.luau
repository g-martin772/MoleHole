local camera_cycle = require("lib/camera_cycle")
local curves = require("lib/curves")
local dquat_lib = require("lib/dquat")
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
    },

    OnStart = function(self: ScriptSelf)
        self.vel = vector.zero
        self.turn = vector.zero
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
        local entity = self.entity
        local anchor = scene.get64(target, "Transform", "Position")
        local rotation = scene.get64(target, "Transform", "Rotation")
        local scale = zoom.step(self, props, dt)
        local want = rotation * dvec3.new(0, props.Height * scale, props.Distance * scale)
        if self.rel == nil then
            self.rel = dvec3.to_vec(scene.get64(entity, "Transform", "Position") - anchor)
        end
        local rel, vel = curves.smooth_damp(dvec3.from_vec(self.rel), want, dvec3.from_vec(self.vel), props.SmoothTime, dt)
        self.rel, self.vel = dvec3.to_vec(rel), dvec3.to_vec(vel)
        scene.set64(entity, "Transform", "Position", anchor + rel)

        local aim = dquat_lib.look_rotation(dquat_lib.forward(rotation) * props.LookAhead - rel, dquat_lib.up(rotation))
        local current = scene.get64(entity, "Transform", "Rotation")
        local turned, omega = dquat_lib.smooth_damp(current, aim, dvec3.from_vec(self.turn), props.TurnTime, dt)
        self.turn = dvec3.to_vec(omega)
        scene.set64(entity, "Transform", "Rotation", turned)
    end,
}
