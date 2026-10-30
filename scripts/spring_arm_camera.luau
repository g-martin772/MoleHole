local orbit = require("lib/orbit")

return {
    properties = {
        { name = "Target", type = "entity" },
        { name = "ArmLength", type = "float", unit = "m", default = 20000, min = 0.5, max = 1e12 },
        { name = "Yaw", type = "float", default = 0 },
        { name = "Pitch", type = "float", default = 20, min = -89, max = 89 },
        { name = "OrbitSpeed", type = "float", default = 15 },
        { name = "HeightOffset", type = "float", unit = "m", default = 0 },
        { name = "Smoothing", type = "float", default = 0.3, min = 0, max = 5 },
        { name = "LookAtTarget", type = "bool", default = true },
    },

    OnStart = function(self: ScriptSelf)
        self.yaw = self.props.Yaw
    end,

    OnTick = function(self: ScriptSelf, dt: number)
        local props = self.props
        local target = props.Target
        if target == nil or not scene.exists(target) then
            return
        end

        self.yaw += props.OrbitSpeed * dt
        local tx, ty, tz = scene.get_xyz(target, "Transform", "Position")
        ty += props.HeightOffset
        local dx, dy, dz = orbit.direction(self.yaw, props.Pitch)
        local gx, gy, gz = tx + dx * props.ArmLength, ty + dy * props.ArmLength, tz + dz * props.ArmLength

        local cx, cy, cz = scene.get_xyz(self.entity, "Transform", "Position")
        local blend = props.Smoothing > 0 and 1 - 2 ^ (-dt / props.Smoothing) or 1
        local px, py, pz = cx + (gx - cx) * blend, cy + (gy - cy) * blend, cz + (gz - cz) * blend
        scene.set_xyz(self.entity, "Transform", "Position", px, py, pz)

        if props.LookAtTarget then
            scene.set(self.entity, "Transform", "Rotation", scene.look_at(vector.create(px - tx, py - ty, pz - tz), vector.zero))
        end
    end,
}
