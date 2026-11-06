local rigs = require("lib/camera_rigs")

local camera_cycle = {}

function camera_cycle.step(self, mode: string, next_camera: Entity?): boolean
    local entity = self.entity
    if not scene.get(entity, "Camera", "Primary") then
        self.announced = false
        return false
    end
    if not self.announced then
        self.announced = true
        events.emit("camera_mode", mode)
    end
    if next_camera ~= nil and next_camera ~= entity and input.pressed("camera") and scene.exists(next_camera) then
        rigs.release(self)
        scene.set(next_camera, "Camera", "Primary", true)
        events.emit("camera_switch", entity, next_camera)
        self.announced = false
        return false
    end
    return true
end

return camera_cycle
