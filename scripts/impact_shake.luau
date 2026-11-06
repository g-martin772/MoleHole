return {
    properties = {
        { name = "FullImpulse", type = "float", default = 5000, min = 1e-9, max = 1e15 },
        { name = "MinImpulse", type = "float", default = 50, min = 0, max = 1e15 },
    },

    OnContact = function(self: ScriptSelf, other: Entity, point: any, normal: any, impulse: number)
        local props = self.props
        if impulse >= props.MinImpulse then
            events.emit("camera_shake", math.min(impulse / props.FullImpulse, 1))
        end
    end,
}
