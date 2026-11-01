local camera_cycle = require("lib/camera_cycle")

return {
    properties = {
        { name = "Target", type = "entity" },
        { name = "Next", type = "entity" },
        { name = "Offset", type = "vec3" },
    },

    OnLateTick = function(self: ScriptSelf, dt: number)
        local props = self.props
        if not camera_cycle.step(self, "cockpit", props.Next) then
            return
        end
        local target = props.Target
        if target == nil or not scene.exists(target) then
            return
        end
        local rotation = scene.get64(target, "Transform", "Rotation")
        local offset = dvec3.from_vec(props.Offset)
        scene.set64(self.entity, "Transform", "Position", scene.get64(target, "Transform", "Position") + rotation * offset)
        scene.set64(self.entity, "Transform", "Rotation", rotation)
    end,
}
