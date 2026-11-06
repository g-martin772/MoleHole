local camera_cycle = require("lib/camera_cycle")
local curves = require("lib/curves")
local dquat_lib = require("lib/dquat")
local rigs = require("lib/camera_rigs")

return {
    properties = {
        { name = "Target", type = "entity" },
        { name = "Next", type = "entity" },
        { name = "Direction", type = "vec3", default = vector.create(0, 0, 1) },
        { name = "FollowRotation", type = "bool", default = true },
        { name = "Distance", type = "float", unit = "m", default = 100, min = 1e-6, max = 1e12 },
        { name = "EndDistance", type = "float", unit = "m", default = 25, min = 1e-6, max = 1e12 },
        { name = "Delay", type = "float", unit = "s", default = 0, min = 0, max = 1e6 },
        { name = "Duration", type = "float", unit = "s", default = 4, min = 0, max = 1e6 },
        { name = "Ease", type = "string", default = "smoothstep" },
        { name = "Seed", type = "int", default = 0, min = 0, max = 1073741824 },
        { name = "ShakeAngle", type = "float", default = 2, min = 0, max = 90 },
        { name = "ShakeOffset", type = "float", unit = "m", default = 0, min = 0, max = 1e9 },
        { name = "ShakeDecay", type = "float", default = 1.5, min = 0, max = 100 },
        { name = "Handheld", type = "float", default = 0, min = 0, max = 45 },
    },

    OnEvent = function(self: ScriptSelf, name: string, a: any)
        rigs.on_shake(self, name, a)
    end,

    OnLateTick = function(self: ScriptSelf, dt: number)
        local props = self.props
        if not camera_cycle.step(self, "dolly", props.Next) then
            return
        end
        local target = props.Target
        if target == nil or not scene.exists(target) then
            return
        end
        rigs.forget(self)
        local entity = self.entity
        if self.base_fov == nil then
            self.base_fov = scene.get(entity, "Camera", "Fov")
            self.clock = 0
        end
        local clock = self.clock
        self.clock = clock + dt
        local progress = props.Duration > 0 and curves.ease(props.Ease, (clock - props.Delay) / props.Duration) or 1
        local distance = props.Distance + (props.EndDistance - props.Distance) * progress

        local anchor = scene.get64(target, "Transform", "Position")
        local rotation = dquat_lib.IDENTITY
        if props.FollowRotation then
            rotation = scene.get64(target, "Transform", "Rotation")
        end
        local direction = dvec3.normalize(rotation * dvec3.from_vec(props.Direction))
        local position = anchor + direction * distance
        local view = dquat_lib.look_rotation(-direction, dquat_lib.up(rotation))
        scene.set64(entity, "Transform", "Position", position)
        scene.set64(entity, "Transform", "Rotation", view)
        scene.set(entity, "Camera", "Fov", rigs.dolly_fov(self.base_fov, props.Distance, distance))
        rigs.effects(self, props, dt, position, view)
    end,
}
