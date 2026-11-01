local flight = require("lib/flight")
local input_map = require("lib/input_map")

local DYNAMIC = 2

local function config(self, props)
    local inertia = self.inertia
    return {
        thrust_x = props.ThrustX,
        thrust_y = props.ThrustY,
        thrust_z = props.ThrustZ,
        torque_x = props.TorquePitch,
        torque_y = props.TorqueYaw,
        torque_z = props.TorqueRoll,
        inertia_x = inertia.x,
        inertia_y = inertia.y,
        inertia_z = inertia.z,
        max_rate = math.rad(props.MaxRate),
        response = math.max(props.ResponseTime, 1e-9),
        mass = props.MassOverride,
        g_limit = props.GLimit,
        boost = props.Boost,
        retro_stop = props.RetroStop,
        assist = self.assist,
    }
end

local function resolve_inertia(entity, props)
    local given = props.Inertia
    if given.x > 0 and given.y > 0 and given.z > 0 then
        return given
    end
    local mass = scene.get(entity, "RigidBody", "Mass")
    local x, y, z = flight.collider_inertia(entity, mass)
    return vector.create(x, y, z)
end

return {
    properties = {
        { name = "ThrustX", type = "float", default = 1e4, min = 0, max = 1e30 },
        { name = "ThrustY", type = "float", default = 1e4, min = 0, max = 1e30 },
        { name = "ThrustZ", type = "float", default = 2e4, min = 0, max = 1e30 },
        { name = "TorquePitch", type = "float", default = 2e4, min = 0, max = 1e30 },
        { name = "TorqueYaw", type = "float", default = 2e4, min = 0, max = 1e30 },
        { name = "TorqueRoll", type = "float", default = 2e4, min = 0, max = 1e30 },
        { name = "MaxRate", type = "float", default = 90, min = 0, max = 5700 },
        { name = "ResponseTime", type = "float", unit = "s", default = 0.4, min = 1e-9, max = 1e6 },
        { name = "Assist", type = "int", default = 1, min = 0, max = 3 },
        { name = "MassOverride", type = "float", unit = "kg", default = 0, min = 0, max = 1e37 },
        { name = "Inertia", type = "vec3", default = vector.create(0, 0, 0) },
        { name = "GLimit", type = "float", default = 0, min = 0, max = 1e12 },
        { name = "Boost", type = "float", default = 2, min = 1, max = 100 },
        { name = "RetroStop", type = "float", default = 0.5, min = 0, max = 1e12 },
        { name = "MouseSensitivity", type = "float", default = 0.04, min = 0, max = 10 },
        { name = "InvertPitch", type = "bool", default = false },
    },

    OnStart = function(self: ScriptSelf)
        local props = self.props
        self.assist = props.Assist
        self.controlled = true
        self.inertia = resolve_inertia(self.entity, props)
        self.map = input_map.new({
            axes = {
                x = { action = "move_x" },
                y = { action = "move_y" },
                z = { action = "move_z" },
                pitch = { action = "pitch" },
                yaw = { action = "yaw" },
                roll = { action = "roll" },
            },
        })
        self.flight = flight.new(config(self, props))
    end,

    OnEvent = function(self: ScriptSelf, name: string, mode: string)
        if name == "camera_mode" then
            self.controlled = mode ~= "fly"
        end
    end,

    OnFixedTick = function(self: ScriptSelf, dt: number)
        local props = self.props
        local entity = self.entity
        if scene.get(entity, "RigidBody", "Type") ~= DYNAMIC then
            return
        end
        if input.pressed("assist") then
            self.assist = (self.assist + 1) % 4
            events.emit("assist_mode", self.assist)
        end
        self.inertia = resolve_inertia(entity, props)
        flight.configure(self.flight, config(self, props))

        local command = { boost = false }
        if self.controlled then
            input_map.update(self.map, dt)
            local mouse = input.mouse_delta()
            local look = props.MouseSensitivity
            command.x = input_map.axis(self.map, "x")
            command.y = input_map.axis(self.map, "y")
            command.z = input_map.axis(self.map, "z")
            command.pitch = input_map.axis(self.map, "pitch") + mouse.y * look * (props.InvertPitch and 1 or -1)
            command.yaw = input_map.axis(self.map, "yaw") + mouse.x * look
            command.roll = input_map.axis(self.map, "roll")
            command.boost = input.held("boost")
        end
        flight.apply(entity, self.flight, command, dt * time.scale())
    end,
}
