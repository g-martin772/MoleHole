local camera_cycle = require("lib/camera_cycle")
local rigs = require("lib/camera_rigs")
local zoom = require("lib/zoom")

return {
    properties = {
        { name = "Target", type = "entity" },
        { name = "Next", type = "entity" },
        { name = "Offset", type = "vec3" },
        { name = "ZoomMin", type = "float", default = 0.1, min = 1e-6, max = 1e6 },
        { name = "ZoomMax", type = "float", default = 2, min = 1e-6, max = 1e6 },
        { name = "ZoomSpeed", type = "float", default = 0.1, min = 0, max = 10 },
        { name = "ZoomSmooth", type = "float", unit = "s", default = 0.12, min = 0, max = 60 },
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
        if not camera_cycle.step(self, "cockpit", props.Next) then
            return
        end
        local target = props.Target
        if target == nil or not scene.exists(target) then
            return
        end
        rigs.forget(self)
        local position, rotation = rigs.ride_along(scene.get64(target, "Transform", "Position"), scene.get64(target, "Transform", "Rotation"), dvec3.from_vec(props.Offset))
        scene.set64(self.entity, "Transform", "Position", position)
        scene.set64(self.entity, "Transform", "Rotation", rotation)
        local scale = zoom.step(self, props, dt)
        if self.base_fov == nil then
            self.base_fov = scene.get(self.entity, "Camera", "Fov")
        end
        if scale ~= 1 or self.fov_dirty then
            local half = math.atan(math.tan(math.rad(self.base_fov) / 2) * scale)
            scene.set(self.entity, "Camera", "Fov", math.clamp(math.deg(half * 2), 1, 179))
            self.fov_dirty = scale ~= 1
        end
        rigs.effects(self, props, dt, position, rotation)
    end,
}
