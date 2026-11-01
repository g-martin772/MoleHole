local camera_cycle = require("lib/camera_cycle")
local curves = require("lib/curves")

local function aim(yaw: number, pitch: number)
    return dquat.mul(dquat.from_axis_angle(dvec3.new(0, 1, 0), math.rad(yaw)), dquat.from_axis_angle(dvec3.new(1, 0, 0), math.rad(pitch)))
end

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
    },

    OnStart = function(self: ScriptSelf)
        self.vel = self.vel or vector.zero
        self.mouse_free = self.mouse_free or false
        if self.props.StartActive then
            scene.set(self.entity, "Camera", "Primary", true)
        end
    end,

    OnEvent = function(self: ScriptSelf, name: string, a: any, b: any, c: any, d: any, e: any)
        if name == "camera_switch" and b == self.entity then
            self.from = a
        elseif name == "ship_status" then
            self.mouse_free = e == 1
        end
    end,

    OnLateTick = function(self: ScriptSelf, dt: number)
        local props = self.props
        if not camera_cycle.step(self, "frame", props.Next) then
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
        if self.yaw == nil then
            local f = dquat.rotate(scene.get64(entity, "Transform", "Rotation"), dvec3.new(0, 0, -1))
            self.yaw, self.pitch = math.deg(math.atan2(-f.x, -f.z)), math.deg(math.asin(math.clamp(f.y, -1, 1)))
        end

        self.yaw -= input.axis("cam_yaw") * props.KeyOrbitRate * dt
        self.pitch += input.axis("cam_pitch") * props.KeyOrbitRate * dt
        if props.MouseOrbit and self.mouse_free then
            local mouse = input.mouse_delta()
            self.yaw -= mouse.x * props.OrbitSpeed
            self.pitch -= mouse.y * props.OrbitSpeed
        end
        self.pitch = math.clamp(self.pitch, -89, 89)
        local rotation = aim(self.yaw, self.pitch)
        scene.set64(entity, "Transform", "Rotation", rotation)

        local target = props.Target
        if target == nil or not scene.exists(target) then
            return
        end
        local anchor = scene.get64(target, "Transform", "Position")
        local want = rotation * dvec3.new(0, props.Height, props.Distance)
        if self.rel == nil then
            self.rel = dvec3.to_vec(scene.get64(entity, "Transform", "Position") - anchor)
        end
        local rel, vel = curves.smooth_damp(dvec3.from_vec(self.rel), want, dvec3.from_vec(self.vel), props.SmoothTime, dt)
        self.rel, self.vel = dvec3.to_vec(rel), dvec3.to_vec(vel)
        scene.set64(entity, "Transform", "Position", anchor + rel)
    end,
}
