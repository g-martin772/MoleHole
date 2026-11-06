local orbit = require("lib/orbit")

local BODY_REFRESH = 30
local BRAKE = 0.5
local VELOCITY_GAIN = 4
local BURN_DONE = 0.3
local LATE = 1.5

local function alive(entity: Entity?): boolean
    return entity ~= nil and scene.exists(entity)
end

local function velocity_of(entity: Entity): DVec3
    return scene.get64(entity, "Velocity", "Linear") or dvec3.new(0, 0, 0)
end

local function reach(limits, direction: DVec3): number
    local worst = 0
    for index, component in { math.abs(direction.x), math.abs(direction.y), math.abs(direction.z) } do
        if component > 1e-12 then
            local limit = limits[index]
            worst = math.max(worst, limit > 0 and component / limit or math.huge)
        end
    end
    return worst > 0 and 1 / worst or 0
end

local function reset(self)
    self.burn = nil
    self.hold = nil
    self.orbit_radius = nil
end

local function body_of(self, props, position: DVec3)
    local body = self.body
    self.body_age = (self.body_age or 0) + 1
    if body == nil or not scene.exists(body.entity) or self.body_age >= BODY_REFRESH then
        self.body_age = 0
        local found = alive(props.Body) and orbit.body(props.Body) or orbit.dominant(position, self.entity)
        body = found and { entity = found.entity, mass = found.mass, mu = found.mu } or nil
        self.body = body
    end
    if body == nil then
        return nil
    end
    return body.entity, body.mu, scene.get64(body.entity, "Transform", "Position"), velocity_of(body.entity)
end

local function gravity_feed(self, props, position: DVec3): DVec3
    local entity, mu, at = body_of(self, props, position)
    if entity == nil then
        return dvec3.new(0, 0, 0)
    end
    local r = position - at
    local d = dvec3.length(r)
    return d > 0 and r * (mu / (d * d * d)) or dvec3.new(0, 0, 0)
end

local function approach(self, props, position: DVec3, velocity: DVec3, qi: DQuat, goal: DVec3, goal_velocity: DVec3, tolerance: number)
    local to_goal = goal - position
    local distance = dvec3.length(to_goal)
    local relative = velocity - goal_velocity
    local tau = props.ResponseTime
    local direction = distance > 1e-9 and to_goal / distance or dvec3.new(0, 0, 0)
    local wanted = 0
    if distance > 1e-9 then
        local brake = BRAKE * reach(self.limits, dquat.rotate(qi, direction))
        wanted = math.sqrt(brake * brake * tau * tau + 2 * brake * distance) - brake * tau
        if props.CruiseSpeed > 0 then
            wanted = math.min(wanted, props.CruiseSpeed)
        end
    end
    local accel = (direction * wanted - relative) * (VELOCITY_GAIN / tau) + gravity_feed(self, props, position)
    local speed = dvec3.length(relative)
    local arrived = distance < tolerance and speed < props.SpeedTolerance
    return accel, distance > tolerance and direction or nil, arrived, distance, speed
end

local function hold_point(self, props, position: DVec3): (DVec3, DVec3)
    local target = props.Target
    local at = alive(target) and scene.get64(target, "Transform", "Position") or nil
    if self.hold == nil then
        local base = at and position - at or position
        self.hold = { relative = at ~= nil, x = base.x, y = base.y, z = base.z }
    end
    local point = dvec3.new(self.hold.x, self.hold.y, self.hold.z)
    if self.hold.relative and at ~= nil then
        return point + at, velocity_of(target)
    end
    return point, dvec3.new(0, 0, 0)
end

local function guide_goto(self, props, position, velocity, qi)
    local target = props.Target
    if not alive(target) then
        return dvec3.new(0, 0, 0), nil, "NO TARGET", 0, 0
    end
    local at = scene.get64(target, "Transform", "Position")
    local goal = at
    if props.Standoff > 0 then
        local away = position - at
        goal = at + (dvec3.length_squared(away) > 0 and dvec3.normalize(away) or dvec3.new(0, 1, 0)) * props.Standoff
    end
    local accel, face, arrived, distance, speed = approach(self, props, position, velocity, qi, goal, velocity_of(target), props.Tolerance)
    return accel, face, arrived and "ARRIVED" or "APPROACH", distance, speed
end

local function guide_hold(self, props, position, velocity, qi)
    local goal, goal_velocity = hold_point(self, props, position)
    local accel, _, arrived, distance, speed = approach(self, props, position, velocity, qi, goal, goal_velocity, props.Tolerance)
    return accel, nil, arrived and "HOLDING" or "RETURN", distance, speed
end

local function guide_match(self, props, position, velocity, qi)
    local target = props.Target
    local goal = alive(target) and velocity_of(target) or dvec3.new(0, 0, 0)
    local relative = velocity - goal
    local speed = dvec3.length(relative)
    local accel = -relative * (VELOCITY_GAIN / props.ResponseTime) + gravity_feed(self, props, position)
    return accel, nil, speed < props.SpeedTolerance and "MATCHED" or "BRAKE", speed, speed
end

local function guide_orbit(self, props, position, velocity, qi, physical_dt: number)
    local entity, mu, body_position, body_velocity = body_of(self, props, position)
    if entity == nil then
        return dvec3.new(0, 0, 0), nil, "NO BODY", 0, 0
    end
    local r, v = position - body_position, velocity - body_velocity
    local radius = dvec3.length(r)
    self.orbit_radius = self.orbit_radius or radius
    local target = props.OrbitRadius > 0 and props.OrbitRadius or self.orbit_radius
    local el = orbit.elements(mu, r, v)
    local speed = el.speed
    local prograde = speed > 0 and v / speed or dvec3.new(0, 0, -1)
    local tolerance = props.OrbitTolerance
    local burn = self.burn
    if burn == nil then
        local plan = orbit.plan_circular(mu, el, target, tolerance)
        if plan == nil then
            return dvec3.new(0, 0, 0), prograde, el.bound and "STABLE" or "ESCAPE", radius - target, el.e
        end
        local duration = math.abs(plan.dv) / math.max(reach(self.limits, dquat.rotate(qi, prograde)), 1e-12)
        if plan.dt > math.max(0.5 * duration, LATE * physical_dt) then
            return dvec3.new(0, 0, 0), prograde, "COAST", plan.dt, el.e
        end
        burn = { energy = plan.energy, sign = plan.energy > el.energy and 1 or -1 }
        self.burn = burn
    end
    local err = burn.energy - el.energy
    if burn.sign * err <= BURN_DONE * tolerance * math.abs(burn.energy) then
        self.burn = nil
        return dvec3.new(0, 0, 0), prograde, "BURN DONE", radius - target, el.e
    end
    local available = reach(self.limits, dquat.rotate(qi, prograde))
    local magnitude = math.min(math.abs(err) / (speed * props.ResponseTime), available)
    return prograde * (burn.sign * magnitude), prograde, "BURN", radius - target, el.e
end

local function silent(entity: Entity)
    events.emit("autopilot_command", entity, 0, 0, 0, 0, 0, 0, 0)
    events.emit("autopilot_status", entity, 0, 0, "OFF", 0, 0)
end

return {
    properties = {
        { name = "Mode", type = "int", default = 1, min = 0, max = 3 },
        { name = "Target", type = "entity" },
        { name = "Body", type = "entity" },
        { name = "Engaged", type = "bool", default = false },
        { name = "OrbitRadius", type = "float", unit = "m", default = 0, min = 0, max = 1e30 },
        { name = "OrbitTolerance", type = "float", default = 0.002, min = 1e-5, max = 0.5 },
        { name = "Standoff", type = "float", unit = "m", default = 0, min = 0, max = 1e30 },
        { name = "Tolerance", type = "float", unit = "m", default = 5, min = 0, max = 1e30 },
        { name = "SpeedTolerance", type = "float", unit = "m/s", default = 0.5, min = 0, max = 1e30 },
        { name = "CruiseSpeed", type = "float", unit = "m/s", default = 0, min = 0, max = 1e30 },
        { name = "ResponseTime", type = "float", unit = "s", default = 1, min = 1e-9, max = 1e6 },
        { name = "TurnTime", type = "float", unit = "s", default = 1, min = 1e-9, max = 1e6 },
    },

    OnStart = function(self: ScriptSelf)
        if self.engaged == nil then
            self.engaged = self.props.Engaged
        end
        self.mode = self.mode or self.props.Mode
        self.announced = self.announced or false
    end,

    OnEvent = function(self: ScriptSelf, name: string, a: any, b: any, c: any, d: any, e: any)
        if name == "ship_limits" and a == self.entity then
            self.limits = { b, c, d, e }
        end
    end,

    OnFixedTick = function(self: ScriptSelf, dt: number)
        local props = self.props
        local entity = self.entity
        if input.pressed("autopilot") then
            self.engaged = not self.engaged
            reset(self)
        end
        if input.pressed("autopilot_mode") then
            self.mode = (self.mode + 1) % 4
            reset(self)
        end
        if not self.engaged then
            if self.announced then
                self.announced = false
                silent(entity)
            end
            return
        end
        local limits = self.limits
        if limits == nil then
            return
        end
        self.announced = true

        local position = scene.get64(entity, "Transform", "Position")
        local velocity = velocity_of(entity)
        local qi = dquat.inverse(scene.get64(entity, "Transform", "Rotation"))
        local mode = self.mode
        local accel, face, phase, first, second
        if mode == 2 then
            accel, face, phase, first, second = guide_orbit(self, props, position, velocity, qi, dt * time.scale())
        elseif mode == 1 then
            accel, face, phase, first, second = guide_goto(self, props, position, velocity, qi)
        elseif mode == 3 then
            accel, face, phase, first, second = guide_match(self, props, position, velocity, qi)
        else
            accel, face, phase, first, second = guide_hold(self, props, position, velocity, qi)
        end

        local body = dquat.rotate(qi, accel)
        local scale = 0
        for index, component in { math.abs(body.x), math.abs(body.y), math.abs(body.z) } do
            if component > 0 then
                scale = math.max(scale, limits[index] > 0 and component / limits[index] or math.huge)
            end
        end
        if scale > 1 then
            body = body / scale
        end

        local wx, wy = 0, 0
        if face ~= nil then
            local nose = dquat.rotate(qi, face)
            local rate = limits[4]
            wx = math.clamp(math.atan2(nose.y, math.sqrt(nose.x * nose.x + nose.z * nose.z)) / props.TurnTime, -rate, rate)
            wy = -math.clamp(math.atan2(nose.x, -nose.z) / props.TurnTime, -rate, rate)
        end
        events.emit("autopilot_command", entity, 1, body.x, body.y, body.z, wx, wy, 0)
        events.emit("autopilot_status", entity, 1, mode, phase, first, second)
    end,
}
