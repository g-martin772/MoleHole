local camera_cycle = require("lib/camera_cycle")
local zoom = require("lib/zoom")

local function aim(yaw: number, pitch: number)
    return dquat.mul(dquat.from_axis_angle(dvec3.new(0, 1, 0), math.rad(yaw)), dquat.from_axis_angle(dvec3.new(1, 0, 0), math.rad(pitch)))
end

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
    },

    OnEvent = function(self: ScriptSelf, name: string, from: any, to: any)
        if name == "camera_switch" and to == self.entity then
            self.from = from
        end
    end,

    OnLateTick = function(self: ScriptSelf, dt: number)
        local props = self.props
        if not camera_cycle.step(self, "fly", props.Next) then
            return
        end
        local entity = self.entity
        if self.from ~= nil then
            if scene.exists(self.from) then
                scene.set64(entity, "Transform", "Position", scene.get64(self.from, "Transform", "Position"))
                scene.set64(entity, "Transform", "Rotation", scene.get64(self.from, "Transform", "Rotation"))
            end
            self.from, self.yaw, self.rel = nil, nil, nil
            return
        end
        local position = scene.get64(entity, "Transform", "Position")
        if self.yaw == nil then
            local f = dquat.rotate(scene.get64(entity, "Transform", "Rotation"), dvec3.new(0, 0, -1))
            self.yaw, self.pitch = math.deg(math.atan2(-f.x, -f.z)), math.deg(math.asin(math.clamp(f.y, -1, 1)))
        end
        local mouse = input.mouse_delta()
        self.yaw -= mouse.x * props.LookSpeed
        self.pitch = math.clamp(self.pitch - mouse.y * props.LookSpeed, -89, 89)
        local rotation = aim(self.yaw, self.pitch)

        local speed = props.Speed * zoom.step(self, props, dt, true) * (input.held("boost") and props.Boost or 1) * dt
        local move = dquat.rotate(rotation, dvec3.new(input.axis("move_x"), input.axis("move_y"), -input.axis("move_z"))) * speed

        local target = props.Target
        if target ~= nil and scene.exists(target) then
            local anchor = scene.get64(target, "Transform", "Position")
            if self.rel == nil then
                self.rel = dvec3.to_vec(position - anchor)
            end
            local rel = dvec3.from_vec(self.rel) + move
            self.rel = dvec3.to_vec(rel)
            position = anchor + rel
        else
            self.rel = nil
            position = position + move
        end
        scene.set64(entity, "Transform", "Position", position)
        scene.set64(entity, "Transform", "Rotation", rotation)
    end,
}
