local orbit = require("lib/orbit")

return {
    properties = {
        { name = "Range", type = "float", unit = "m", default = 500, min = 1, max = 1e9 },
        { name = "Impulse", type = "float", default = 50, min = 0 },
        { name = "Yaw", type = "float", default = 0 },
        { name = "Pitch", type = "float", default = 0, min = -89, max = 89 },
        { name = "Mask", type = "float", default = -1 },
    },

    OnTick = function(self: ScriptSelf, dt: number)
        if not input.pressed("fire") then
            return
        end
        local props = self.props
        local dx, dy, dz = orbit.direction(props.Yaw, props.Pitch)
        local origin = scene.get64(self.entity, "Transform", "Position")
        local hit = physics.raycast(origin, vector.create(dx, dy, dz), props.Range, props.Mask)
        if hit == nil then
            return
        end
        local rb = scene.get(hit.entity, "RigidBody", "Type")
        if rb == 2 then
            physics.add_impulse(hit.entity, vector.create(dx, dy, dz) * props.Impulse, hit.point64)
        end
        events.emit("ray_hit", hit.entity, hit.distance)
    end,
}
