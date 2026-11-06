local flight = require("lib/flight")
local input_map = require("lib/input_map")

local DYNAMIC = 2
local AUTOPILOT_STALE = 3

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
        assist = self.assist_override or self.assist,
        max_speed = props.MaxSpeed,
        hold_speed = self.hold_speed,
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

local function accel_limits(self, props)
    local mass = props.MassOverride > 0 and props.MassOverride or math.clamp(scene.get(self.entity, "RigidBody", "Mass"), 1e-4, 1e37)
    local cap = props.GLimit > 0 and props.GLimit * flight.G or math.huge
    return math.min(props.ThrustX / mass, cap), math.min(props.ThrustY / mass, cap), math.min(props.ThrustZ / mass, cap), math.rad(props.MaxRate)
end

local function share(value: number, limit: number): number
    return limit > 0 and value / limit or 0
end

local function steer(self, command, ax: number, ay: number, az: number, rate: number)
    local ap = self.ap
    local function free(value: number?): boolean
        return math.abs(value or 0) < 1e-4
    end
    if free(command.x) then
        command.x = share(ap.x, ax)
    end
    if free(command.y) then
        command.y = share(ap.y, ay)
    end
    if free(command.z) and free(self.throttle) then
        command.z = -share(ap.z, az)
        command.throttle = 0
    end
    if free(command.pitch) then
        command.pitch = share(ap.wx, rate)
    end
    if free(command.yaw) then
        command.yaw = -share(ap.wy, rate)
    end
    if free(command.roll) then
        command.roll = -share(ap.wz, rate)
    end
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
        { name = "ControlMode", type = "int", default = 0, min = 0, max = 1 },
        { name = "KeyTurn", type = "float", default = 1, min = 0, max = 1 },
        { name = "KeySlew", type = "float", default = 0, min = 0, max = 1000 },
        { name = "MaxSpeed", type = "float", unit = "m/s", default = 0, min = 0, max = 1e12 },
        { name = "ThrottleRate", type = "float", default = 0.5, min = 0, max = 100 },
        { name = "ThrottleMin", type = "float", default = 0, min = -1, max = 0 },
    },

    OnStart = function(self: ScriptSelf)
        local props = self.props
        self.assist = props.Assist
        self.controlled = true
        self.control_mode = self.control_mode or props.ControlMode
        self.throttle = self.throttle or 0
        self.hold_speed = self.hold_speed or -1
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
        self.ap = self.ap or { x = 0, y = 0, z = 0, wx = 0, wy = 0, wz = 0, age = AUTOPILOT_STALE + 1, active = false, on = false }
        self.flight = flight.new(config(self, props))
    end,

    OnEvent = function(self: ScriptSelf, name: string, a: any, b: any, c: any, d: any, e: any, f: any, g: any, h: any)
        if name == "camera_mode" then
            self.controlled = a ~= "fly"
        elseif name == "autopilot_command" and a == self.entity then
            local ap = self.ap
            ap.active, ap.x, ap.y, ap.z, ap.wx, ap.wy, ap.wz, ap.age = b == 1, c, d, e, f, g, h, 0
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
        if input.pressed("control_mode") then
            self.control_mode = 1 - self.control_mode
        end
        self.inertia = resolve_inertia(entity, props)

        local command = { boost = false }
        if self.controlled then
            local axes = self.map.axes
            for _, name in { "pitch", "yaw", "roll" } do
                axes[name].sensitivity = props.KeyTurn
                axes[name].slew = props.KeySlew > 0 and props.KeySlew or nil
            end
            input_map.update(self.map, dt)
            local mouse = self.control_mode == 0 and input.mouse_delta() or vector.zero
            local look = props.MouseSensitivity
            command.x = input_map.axis(self.map, "x")
            command.y = input_map.axis(self.map, "y")
            command.z = input_map.axis(self.map, "z")
            command.pitch = input_map.axis(self.map, "pitch") + mouse.y * look * (props.InvertPitch and 1 or -1)
            command.yaw = input_map.axis(self.map, "yaw") + mouse.x * look
            command.roll = input_map.axis(self.map, "roll")
            command.boost = input.held("boost")
            command.kill = input.held("kill_velocity")

            local ramp = input.axis("throttle")
            if math.abs(ramp) > 1e-4 then
                self.throttle = math.clamp(self.throttle + ramp * props.ThrottleRate * dt, props.ThrottleMin, 1)
                self.hold_speed = -1
            end
            if input.pressed("throttle_cut") then
                self.throttle = 0
            end
            if input.pressed("speed_hold") then
                if self.hold_speed >= 0 then
                    self.hold_speed = -1
                else
                    self.hold_speed = dvec3.length(scene.get64(entity, "Velocity", "Linear") or dvec3.new(0, 0, 0))
                    self.throttle = 0
                end
            end
            if command.kill then
                self.throttle, self.hold_speed = 0, -1
            end
        end
        command.throttle = self.throttle
        local ap = self.ap
        local ax, ay, az, rate = accel_limits(self, props)
        ap.age += 1
        local steered = ap.active and ap.age <= AUTOPILOT_STALE and not command.kill
        if steered then
            if not ap.on then
                self.throttle, self.hold_speed = 0, -1
                command.throttle = 0
            end
            steer(self, command, ax, ay, az, rate)
            self.assist_override = flight.RATE
        else
            self.assist_override = nil
        end
        ap.on = steered
        local limits = self.limits
        if limits == nil or limits[1] ~= ax or limits[2] ~= ay or limits[3] ~= az or limits[4] ~= rate then
            self.limits = { ax, ay, az, rate }
            events.emit("ship_limits", self.entity, ax, ay, az, rate)
        end
        flight.configure(self.flight, config(self, props))
        local info = flight.apply(entity, self.flight, command, dt * time.scale())
        events.emit("ship_status", self.throttle, self.assist, info.boosted and 1 or 0, info.g, self.control_mode, self.hold_speed, info.killing and 1 or 0)
    end,
}
