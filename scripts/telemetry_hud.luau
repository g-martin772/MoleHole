local hud = require("lib/hud")

local ASSIST_NAMES = { [0] = "OFF", "RATE", "HOLD", "RETRO" }
local WHITE, DIM, GOLD, GREEN, RED, CYAN = "#e8f0ff", "#9fb0c8", "#ffc040", "#50ff80", "#ff6060", "#60d8ff"
local ZERO = dvec3.new(0, 0, 0)
local REFRESH_TICKS = 15

local function velocity_of(entity: Entity): DVec3
    return scene.get64(entity, "Velocity", "Linear") or ZERO
end

local function primary_camera(self): Entity?
    local camera = self.camera
    if camera ~= nil and scene.exists(camera) and scene.get(camera, "Camera", "Primary") then
        return camera
    end
    for _, candidate in scene.query("Camera") do
        if scene.get(candidate, "Camera", "Primary") then
            self.camera = candidate
            return candidate
        end
    end
    self.camera = nil
    return nil
end

local function title(entity: Entity): string
    return scene.name(entity) or "target"
end

local function draw_markers(self, props, center, position, velocity, ref_velocity, size: number)
    local camera = primary_camera(self)
    if camera == nil then
        return
    end
    local camera_q = scene.get64(camera, "Transform", "Rotation")
    local fov = scene.get(camera, "Camera", "Fov")
    local relative = velocity - ref_velocity
    local aspect = props.Aspect
    local function place(direction, kind: string, color: string, label: string?)
        local point, angle, front = hud.project(direction, camera_q, fov)
        local inside = front and point ~= nil and math.abs(point.x) < 0.47 * aspect and point.y > 0.03 and point.y < 0.97
        if inside then
            hud.marker(center, kind, point.x, point.y, 0.016, { color = color })
            if label then
                center.text(point.x + 0.026, point.y - 0.004, label, { size = size, color = color })
            end
        elseif kind ~= "retrograde" then
            local x, y = hud.edge_arrow(center, angle, 0.36, 0.022, { color = color })
            if label then
                center.text(x - math.sin(angle) * 0.04, y + math.cos(angle) * 0.04, label, { size = size, align = "center", color = color })
            end
        end
    end
    if dvec3.length(relative) > 1e-3 then
        local unit = dvec3.normalize(relative)
        place(unit, "prograde", GREEN, nil)
        place(-unit, "retrograde", RED, nil)
    end
    local target = props.Target
    if target ~= nil and scene.exists(target) then
        local offset = scene.get64(target, "Transform", "Position") - position
        if dvec3.length(offset) > 0 then
            place(dvec3.normalize(offset), "target", CYAN, title(target))
        end
    end
end

return {
    properties = {
        { name = "Subject", type = "entity" },
        { name = "Reference", type = "entity" },
        { name = "Target", type = "entity" },
        { name = "Level", type = "int", default = 2, min = 0, max = 2 },
        { name = "Scale", type = "float", default = 1, min = 0.5, max = 3 },
        { name = "Aspect", type = "float", default = 1.7, min = 0.5, max = 4 },
        { name = "Attitude", type = "bool", default = true },
        { name = "Markers", type = "bool", default = true },
        { name = "Environment", type = "bool", default = true },
    },

    OnStart = function(self: ScriptSelf)
        self.level = self.level or self.props.Level
        self.status = self.status or { throttle = 0, assist = 1, boost = 0, g = 0, mode = 0, hold = -1, kill = 0 }
        self.camera_mode = self.camera_mode or "-"
        self.tick = self.tick or 0
        self.age = self.age or 0
        self.proper = self.proper or 0
    end,

    OnEvent = function(self: ScriptSelf, name: string, a: any, b: any, c: any, d: any, e: any, f: any, g: any)
        if name == "ship_status" then
            local status = self.status
            status.throttle, status.assist, status.boost, status.g, status.mode, status.hold, status.kill = a, b, c, d, e, f, g
        elseif name == "camera_mode" then
            self.camera_mode = a
        end
    end,

    OnLateTick = function(self: ScriptSelf, dt: number)
        local props = self.props
        if input.pressed("hud") then
            self.level = (self.level + 2) % 3
        end
        local subject = props.Subject or self.entity
        if not scene.exists(subject) then
            return
        end
        local rate = time.clockrate(subject)
        local seconds = dt * time.scale()
        self.age += seconds
        self.proper += rate * seconds
        if self.level == 0 then
            return
        end
        self.tick += 1

        local left, right, center = ui.hud.at("left"), ui.hud.at("right"), ui.hud.at("center")
        local text_size = 15 * props.Scale
        local big = 26 * props.Scale
        local full = self.level == 2

        local position = scene.get64(subject, "Transform", "Position")
        local rotation = scene.get64(subject, "Transform", "Rotation")
        local velocity = velocity_of(subject)
        local reference = props.Reference
        local has_reference = reference ~= nil and scene.exists(reference)
        local ref_velocity = has_reference and velocity_of(reference) or ZERO
        local speed = dvec3.length(velocity)
        local att = hud.attitude(rotation)
        local status = self.status

        local row = 0.2
        local step = text_size / 720 * 1.35
        local function line(text: string, color: string?, size: number?)
            left.text(0.02, row, text, { size = size or text_size, color = color or WHITE })
            row += (size or text_size) / 720 * 1.35
        end

        left.text(0.02, row, "SPEED", { size = text_size * 0.8, color = DIM })
        row += step * 0.9
        line(hud.format_speed(speed), WHITE, big)
        if speed > 1e5 then
            line(hud.format_beta(speed) .. "   gamma " .. string.format("%.4f", hud.gamma(speed)), CYAN)
        end
        if math.abs(rate - 1) > 1e-12 then
            line("dtau/dt " .. hud.format_rate(rate), CYAN)
        end
        if has_reference then
            local relative = velocity - ref_velocity
            line("REL " .. title(reference) .. "  " .. hud.format_speed(dvec3.length(relative)), GOLD)
        end
        if full then
            local body_rate = hud.to_body(rotation, scene.get64(subject, "Velocity", "Angular") or ZERO)
            line(string.format("G %.2f   ACC %s", status.g, hud.format_accel(status.g * 9.80665)), WHITE)
            line(string.format("RATE  P %+.1f  Y %+.1f  R %+.1f deg/s", math.deg(body_rate.x), math.deg(-body_rate.y), math.deg(-body_rate.z)), DIM)
            if math.abs(rate - 1) > 1e-12 then
                line("CLOCK " .. hud.format_offset(self.proper - self.age) .. " vs far", DIM)
            end
            if props.Environment then
                if self.tick % REFRESH_TICKS == 1 or self.nearest == nil then
                    self.nearest = hud.nearest_body(position, subject)
                end
                local near = self.nearest
                if near ~= nil then
                    local text = near.name .. "  " .. hud.format_distance(near.distance)
                    if near.in_rg then
                        text ..= string.format("  (%.1f rg)", near.in_rg)
                    end
                    line(text, WHITE)
                    if near.altitude then
                        line("ALT " .. hud.format_distance(near.altitude), DIM)
                    end
                end
            end
            local target = props.Target
            if target ~= nil and scene.exists(target) then
                local offset = scene.get64(target, "Transform", "Position") - position
                local range = dvec3.length(offset)
                local closing = range > 0 and -dvec3.dot(velocity - velocity_of(target), offset) / range or 0
                line("TGT " .. title(target) .. "  " .. hud.format_distance(range), CYAN)
                line("CLOSING " .. hud.format_speed(closing), CYAN)
            end
        end

        local bar_x, bar_top, bar_height = -0.05, 0.4, 0.22
        right.text(-0.02, bar_top - 0.03, string.format("THR %3d%%", math.floor(status.throttle * 100 + 0.5)), { size = text_size, align = "right", color = WHITE })
        self.reverse = self.reverse or status.throttle < 0
        hud.vbar(right, bar_x, bar_top, 0.018, bar_height, status.throttle, self.reverse and -1 or 0, 1, { color = status.throttle < 0 and RED or GOLD })
        local info_row = bar_top + bar_height + 0.025
        local function info(text: string, color: string?)
            right.text(-0.02, info_row, text, { size = text_size, align = "right", color = color or WHITE })
            info_row += text_size / 720 * 1.35
        end
        info("ASSIST " .. (ASSIST_NAMES[status.assist] or "?"))
        info(status.boost == 1 and "BOOST ON" or "BOOST off", status.boost == 1 and GOLD or DIM)
        if status.kill == 1 then
            info("KILL VELOCITY", RED)
        elseif status.hold >= 0 then
            info("SPEED HOLD " .. hud.format_speed(status.hold), GREEN)
        end
        if full then
            info("CAMERA " .. string.upper(self.camera_mode))
            info(status.mode == 1 and "CONTROL keys only" or "CONTROL mouse + keys", DIM)
        end

        if props.Attitude then
            local cx, cy, r = 0, 0.83, 0.075
            hud.attitude_ball(center, cx, cy, r, att, hud.to_body(rotation, velocity - ref_velocity))
            hud.triad(center, cx - 0.17, cy, 0.04, att, text_size * 0.8)
            center.text(cx, cy + r + 0.028, string.format("HDG %03d   PIT %s   ROL %s", math.floor(att.heading + 0.5) % 360, hud.format_angle(att.pitch), hud.format_angle(att.roll)), { size = text_size, align = "center", color = WHITE })
        end

        if props.Markers then
            draw_markers(self, props, center, position, velocity, ref_velocity, text_size * 0.9)
        end
    end,
}
