-- Orbits the entity it is attached to around Target at a fixed arm length, like a spring-arm camera boom.
-- Attach it to a camera entity and turn on "possess scene camera" to look through it while playing.
--
-- Script contract: return a table with optional `properties` (shown in the inspector), `OnStart(self)` and
-- `OnTick(self, dt)`. `self.entity` is the owning entity, `self.props` the current property values.

local function armDirection(yawDegrees, pitchDegrees)
    local yaw, pitch = math.rad(yawDegrees), math.rad(pitchDegrees)
    return vector.create(math.cos(pitch) * math.sin(yaw), math.sin(pitch), math.cos(pitch) * math.cos(yaw))
end

return {
    properties = {
        { name = "Target", type = "entity" },
        { name = "ArmLength", type = "float", default = 12, min = 0.5, max = 500 },
        { name = "Yaw", type = "float", default = 0 },
        { name = "Pitch", type = "float", default = 20, min = -89, max = 89 },
        { name = "OrbitSpeed", type = "float", default = 15 },
        { name = "HeightOffset", type = "float", default = 0 },
        { name = "Smoothing", type = "float", default = 0.3, min = 0, max = 5 },
        { name = "LookAtTarget", type = "bool", default = true },
    },

    OnStart = function(self)
        self.yaw = self.props.Yaw
    end,

    -- Yaw is the starting angle in degrees, OrbitSpeed is degrees per second, and Smoothing is the half-life in
    -- seconds of the camera's approach to its goal (0 snaps), so the motion looks the same at any frame rate.
    OnTick = function(self, dt)
        local props = self.props
        local target = props.Target
        if target == nil or not scene.exists(target) then
            return
        end

        self.yaw += props.OrbitSpeed * dt
        local pivot = scene.get(target, "Transform", "Position") + vector.create(0, props.HeightOffset, 0)
        local goal = pivot + armDirection(self.yaw, props.Pitch) * props.ArmLength

        local current = scene.get(self.entity, "Transform", "Position")
        local blend = props.Smoothing > 0 and 1 - 2 ^ (-dt / props.Smoothing) or 1
        local position = current + (goal - current) * blend
        scene.set(self.entity, "Transform", "Position", position)

        if props.LookAtTarget then
            scene.set(self.entity, "Transform", "Rotation", scene.look_at(position, pivot))
        end
    end,
}
