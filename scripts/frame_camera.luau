local camera_cycle = require("lib/camera_cycle")
local rigs = require("lib/camera_rigs")
local zoom = require("lib/zoom")

return {
    properties = {
        { name = "Target", type = "entity" },
        { name = "Next", type = "entity" },
        { name = "Distance", type = "float", unit = "m", default = 300, min = 0, max = 1e12 },
        { name = "Height", type = "float", unit = "m", default = 60, min = -1e12, max = 1e12 },
        { name = "SmoothTime", type = "float", unit = "s", default = 0.25, min = 0, max = 60 },
        { name = "MouseOrbit", type = "bool", default = true },
        { name = "OrbitSpeed", type = "float", default = 0.15, min = 0, max = 10 },
        { name = "KeyOrbitRate", type = "float", unit = "deg/s", default = 60, min = 0, max = 1000 },
        { name = "StartActive", type = "bool", default = false },
        { name = "ZoomMin", type = "float", default = 0.1, min = 1e-6, max = 1e6 },
        { name = "ZoomMax", type = "float", default = 10, min = 1e-6, max = 1e6 },
        { name = "ZoomSpeed", type = "float", default = 0.1, min = 0, max = 10 },
        { name = "ZoomSmooth", type = "float", unit = "s", default = 0.12, min = 0, max = 60 },
        { name = "Seed", type = "int", default = 0, min = 0, max = 1073741824 },
        { name = "ShakeAngle", type = "float", default = 2, min = 0, max = 90 },
        { name = "ShakeOffset", type = "float", unit = "m", default = 0, min = 0, max = 1e9 },
        { name = "ShakeDecay", type = "float", default = 1.5, min = 0, max = 100 },
        { name = "Handheld", type = "float", default = 0, min = 0, max = 45 },
    },

    OnStart = function(self: ScriptSelf)
        self.vel = self.vel or vector.zero
        self.mouse_free = self.mouse_free or false
        if self.props.StartActive then
            scene.set(self.entity, "Camera", "Primary", true)
        end
    end,

    OnEvent = function(self: ScriptSelf, name: string, a: any, b: any, c: any, d: any, e: any)
        rigs.on_switch(self, name, a, b)
        rigs.on_shake(self, name, a)
        if name == "ship_status" then
            self.mouse_free = e == 1
        end
    end,

    OnLateTick = function(self: ScriptSelf, dt: number)
        local props = self.props
        if not camera_cycle.step(self, "frame", props.Next) then
            return
        end
        local entity = self.entity
        if rigs.handover(self) then
            return
        end
        rigs.read_aim(self)

        local dyaw = -input.axis("cam_yaw") * props.KeyOrbitRate * dt
        local dpitch = input.axis("cam_pitch") * props.KeyOrbitRate * dt
        if props.MouseOrbit and self.mouse_free then
            local mouse = input.mouse_delta()
            dyaw -= mouse.x * props.OrbitSpeed
            dpitch -= mouse.y * props.OrbitSpeed
        end
        rigs.turn(self, dyaw, dpitch)
        local rotation = rigs.aim(self.yaw, self.pitch)
        scene.set64(entity, "Transform", "Rotation", rotation)
        local scale = zoom.step(self, props, dt)

        local target = props.Target
        if target == nil or not scene.exists(target) then
            return
        end
        local anchor = scene.get64(target, "Transform", "Position")
        local _, position = rigs.follow(self, anchor, rotation, props.Height * scale, props.Distance * scale, props.SmoothTime, dt)
        rigs.effects(self, props, dt, position, rotation)
    end,
}
