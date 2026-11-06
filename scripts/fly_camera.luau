local camera_cycle = require("lib/camera_cycle")
local rigs = require("lib/camera_rigs")
local zoom = require("lib/zoom")

return {
    properties = {
        { name = "Target", type = "entity" },
        { name = "Next", type = "entity" },
        { name = "Speed", type = "float", default = 150, min = 0, max = 1e12 },
        { name = "Boost", type = "float", default = 4, min = 1, max = 1000 },
        { name = "LookSpeed", type = "float", default = 0.15, min = 0, max = 10 },
        { name = "ZoomMin", type = "float", default = 0.05, min = 1e-6, max = 1e6 },
        { name = "ZoomMax", type = "float", default = 50, min = 1e-6, max = 1e6 },
        { name = "ZoomSpeed", type = "float", default = 0.1, min = 0, max = 10 },
        { name = "ZoomSmooth", type = "float", unit = "s", default = 0.12, min = 0, max = 60 },
        { name = "Seed", type = "int", default = 0, min = 0, max = 1073741824 },
        { name = "ShakeAngle", type = "float", default = 2, min = 0, max = 90 },
        { name = "ShakeOffset", type = "float", unit = "m", default = 0, min = 0, max = 1e9 },
        { name = "ShakeDecay", type = "float", default = 1.5, min = 0, max = 100 },
        { name = "Handheld", type = "float", default = 0, min = 0, max = 45 },
    },

    OnEvent = function(self: ScriptSelf, name: string, from: any, to: any)
        rigs.on_switch(self, name, from, to)
        rigs.on_shake(self, name, from)
    end,

    OnLateTick = function(self: ScriptSelf, dt: number)
        local props = self.props
        if not camera_cycle.step(self, "fly", props.Next) then
            return
        end
        if rigs.handover(self) then
            return
        end
        rigs.read_aim(self)
        local mouse = input.mouse_delta()
        rigs.turn(self, -mouse.x * props.LookSpeed, -mouse.y * props.LookSpeed)
        local rotation = rigs.aim(self.yaw, self.pitch)

        local speed = props.Speed * zoom.step(self, props, dt, true) * (input.held("boost") and props.Boost or 1) * dt
        local move = dquat.rotate(rotation, dvec3.new(input.axis("move_x"), input.axis("move_y"), -input.axis("move_z"))) * speed

        local target = props.Target
        local anchor = nil
        if target ~= nil and scene.exists(target) then
            anchor = scene.get64(target, "Transform", "Position")
        end
        local position = rigs.fly_move(self, rotation, move, anchor)
        rigs.effects(self, props, dt, position, rotation)
    end,
}
