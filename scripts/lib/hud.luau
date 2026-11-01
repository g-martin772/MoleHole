local hud = {}

hud.C = units.const("c")
local GOLD, WHITE = "#ffc040", "#e8f0ff"

function hud.format_speed(v: number): string
    v = math.abs(v)
    if v < 1e3 then
        return string.format("%.1f m/s", v)
    elseif v < 1e6 then
        return string.format("%.2f km/s", v / 1e3)
    end
    return string.format("%.2f Mm/s", v / 1e6)
end

function hud.format_beta(v: number): string
    local beta = math.abs(v) / hud.C
    if beta >= 0.99 then
        return string.format("%.6f c", beta)
    end
    return string.format("%.4f c", beta)
end

function hud.format_distance(m: number): string
    local a = math.abs(m)
    if a < 1e3 then
        return string.format("%.1f m", m)
    elseif a < 1e6 then
        return string.format("%.2f km", m / 1e3)
    elseif a < 1e9 then
        return string.format("%.2f Mm", m / 1e6)
    elseif a < 1.5e12 then
        return string.format("%.2f Gm", m / 1e9)
    elseif a < 0.1 * units.const("ly") then
        return string.format("%.3f AU", m / units.const("AU"))
    end
    return string.format("%.3f ly", m / units.const("ly"))
end

function hud.format_angle(degrees: number): string
    return string.format("%+.1f", degrees)
end

function hud.format_accel(a: number): string
    local abs = math.abs(a)
    if abs < 1e4 then
        return string.format("%.1f m/s2", a)
    end
    return string.format("%.3g m/s2", a)
end

function hud.gamma(speed: number): number
    local beta = math.min(math.abs(speed) / hud.C, 1 - 1e-12)
    return 1 / math.sqrt(1 - beta * beta)
end

function hud.text_gauge(value: number, lo: number, hi: number, width: number?): string
    local n = width or 10
    local u = hi > lo and math.clamp((value - lo) / (hi - lo), 0, 1) or 0
    local filled = math.floor(u * n + 0.5)
    return "[" .. string.rep("#", filled) .. string.rep("-", n - filled) .. "]"
end

function hud.attitude(q: DQuat)
    local forward = dquat.rotate(q, dvec3.new(0, 0, -1))
    local up = dquat.rotate(q, dvec3.new(0, 1, 0))
    local right = dquat.rotate(q, dvec3.new(1, 0, 0))
    local heading = math.deg(math.atan2(forward.x, -forward.z))
    return {
        forward = forward,
        up = up,
        right = right,
        heading = heading % 360,
        pitch = math.deg(math.asin(math.clamp(forward.y, -1, 1))),
        roll = math.deg(math.atan2(-right.y, up.y)),
    }
end

function hud.to_body(q: DQuat, v: DVec3): DVec3
    return dquat.rotate(dquat.inverse(q), v)
end

function hud.project(direction: DVec3, camera: DQuat, fov_degrees: number)
    local local_dir = dquat.rotate(dquat.inverse(camera), direction)
    local depth = -local_dir.z
    local tan_half = math.tan(math.rad(fov_degrees) * 0.5)
    if depth <= 1e-9 then
        return nil, math.atan2(local_dir.x, local_dir.y), false
    end
    local x = 0.5 * local_dir.x / (depth * tan_half)
    local y = 0.5 - 0.5 * local_dir.y / (depth * tan_half)
    return { x = x, y = y }, math.atan2(local_dir.x, local_dir.y), true
end

function hud.body_marker(body: DVec3, span_degrees: number?)
    local span = math.rad(span_degrees or 60)
    local ahead = -body.z
    local azimuth = math.atan2(body.x, ahead)
    local elevation = math.atan2(body.y, math.sqrt(body.x * body.x + ahead * ahead))
    return azimuth / span, -elevation / span, ahead > 0
end

local function mass_of(entity: Entity): (number?, number?)
    if scene.get(entity, "BlackHole", "Mass") ~= nil then
        local mass = scene.get(entity, "BlackHole", "Mass")
        local rg = units.const("G") * mass / (units.const("c") ^ 2)
        return 2 * rg, rg
    end
    local radius = scene.get(entity, "Sphere", "Radius")
    if radius ~= nil then
        return radius, nil
    end
    return nil, nil
end

function hud.nearest_body(position: DVec3, exclude: Entity?)
    local best, best_distance
    for _, component in { "BlackHole", "Sphere" } do
        for _, entity in scene.query(component) do
            if entity ~= exclude then
                local distance = dvec3.distance(position, scene.get64(entity, "Transform", "Position"))
                if best_distance == nil or distance < best_distance then
                    best, best_distance = entity, distance
                end
            end
        end
    end
    if best == nil then
        return nil
    end
    local radius, rg = mass_of(best)
    return {
        entity = best,
        name = scene.name(best) or "body",
        distance = best_distance,
        radius = radius,
        altitude = radius and best_distance - radius or nil,
        in_rg = rg and best_distance / rg or nil,
    }
end

function hud.bar(h, x: number, y: number, w: number, height: number, fraction: number, options)
    local opts = options or {}
    local u = math.clamp(fraction, 0, 1)
    h.rect(x, y, w, height, { color = opts.back or "#00000080", filled = true })
    if u > 0 then
        h.rect(x, y, w * u, height, { color = opts.color or GOLD, filled = true })
    end
    h.rect(x, y, w, height, { color = opts.border or WHITE, thickness = 1 })
end

function hud.vbar(h, x: number, y: number, w: number, height: number, value: number, lo: number, hi: number, options)
    local opts = options or {}
    local span = hi - lo
    local u = span > 0 and math.clamp((value - lo) / span, 0, 1) or 0
    local zero = span > 0 and math.clamp((0 - lo) / span, 0, 1) or 0
    h.rect(x, y, w, height, { color = opts.back or "#00000080", filled = true })
    local top, bottom = y + height * (1 - math.max(u, zero)), y + height * (1 - math.min(u, zero))
    if bottom > top then
        h.rect(x, top, w, bottom - top, { color = opts.color or GOLD, filled = true })
    end
    h.rect(x, y, w, height, { color = opts.border or WHITE, thickness = 1 })
    if lo < 0 then
        local at = y + height * (1 - zero)
        h.line(x - w * 0.3, at, x + w * 1.3, at, { color = opts.border or WHITE, thickness = 1 })
    end
end

function hud.marker(h, kind: string, x: number, y: number, r: number, options)
    local opts = options or {}
    local color, thickness = opts.color or GOLD, opts.thickness or 2
    if kind == "prograde" then
        h.circle(x, y, r, { color = color, thickness = thickness })
        h.circle(x, y, r * 0.12, { color = color, filled = true })
        h.line(x - r * 1.9, y, x - r, y, { color = color, thickness = thickness })
        h.line(x + r, y, x + r * 1.9, y, { color = color, thickness = thickness })
        h.line(x, y - r * 1.9, x, y - r, { color = color, thickness = thickness })
    elseif kind == "retrograde" then
        h.circle(x, y, r, { color = color, thickness = thickness })
        local d = r * 0.7
        h.line(x - d, y - d, x + d, y + d, { color = color, thickness = thickness })
        h.line(x - d, y + d, x + d, y - d, { color = color, thickness = thickness })
    else
        h.rect(x - r, y - r, r * 2, r * 2, { color = color, thickness = thickness })
        h.line(x - r * 1.5, y, x - r, y, { color = color, thickness = thickness })
        h.line(x + r, y, x + r * 1.5, y, { color = color, thickness = thickness })
    end
end

function hud.edge_arrow(h, angle: number, radius: number, size: number, options)
    local opts = options or {}
    local color = opts.color or GOLD
    local dx, dy = math.sin(angle), -math.cos(angle)
    local cx, cy = 0, 0.5
    local tipx, tipy = cx + dx * radius, cy + dy * radius
    local bx, by = tipx - dx * size, tipy - dy * size
    h.line(tipx, tipy, bx - dy * size * 0.5, by + dx * size * 0.5, { color = color, thickness = 2 })
    h.line(tipx, tipy, bx + dy * size * 0.5, by - dx * size * 0.5, { color = color, thickness = 2 })
    return tipx, tipy
end

local SCALE_DEG = 60

function hud.attitude_ball(h, cx: number, cy: number, r: number, att, velocity_body: DVec3?, options)
    local opts = options or {}
    local sky, ground, line = opts.sky or "#4aa3ff", opts.ground or "#b0702a", opts.color or WHITE
    local bank = math.rad(att.roll)
    local dirx, diry = math.cos(bank), -math.sin(bank)
    local downx, downy = math.sin(bank), math.cos(bank)
    local base = r * att.pitch / SCALE_DEG
    local function chord(offset: number, half_scale: number, color, thickness: number)
        if math.abs(offset) >= r then
            return
        end
        local len = math.sqrt(r * r - offset * offset) * half_scale
        local mx, my = cx + downx * offset, cy + downy * offset
        h.line(mx - dirx * len, my - diry * len, mx + dirx * len, my + diry * len, { color = color, thickness = thickness })
    end

    h.circle(cx, cy, r, { color = "#00000070", filled = true })
    for k = 1, 8 do
        chord(base + r * k / 9, 1, hud.fade(ground, 0.35), 3)
    end
    for k = 1, 8 do
        chord(base - r * k / 9, 1, hud.fade(sky, 0.2), 3)
    end
    chord(base, 1, line, 2)
    for _, degrees in { -60, -30, 30, 60 } do
        chord(base + r * degrees / SCALE_DEG * -1, 0.28, hud.fade(line, 0.7), 1)
    end
    h.circle(cx, cy, r, { color = line, thickness = 2 })
    h.line(cx, cy - r, cx, cy - r * 0.9, { color = GOLD, thickness = 2 })

    h.line(cx - r * 0.45, cy, cx - r * 0.15, cy, { color = GOLD, thickness = 3 })
    h.line(cx + r * 0.15, cy, cx + r * 0.45, cy, { color = GOLD, thickness = 3 })
    h.circle(cx, cy, r * 0.06, { color = GOLD, thickness = 2 })

    if velocity_body ~= nil and dvec3.length(velocity_body) > 1e-6 then
        for _, sign in { 1, -1 } do
            local u, v, ahead = hud.body_marker(velocity_body * sign, SCALE_DEG)
            if ahead then
                local length = math.sqrt(u * u + v * v)
                local k = length > 0.95 and 0.95 / length or 1
                hud.marker(h, sign == 1 and "prograde" or "retrograde", cx + u * k * r, cy + v * k * r, r * 0.11, { color = sign == 1 and "#50ff80" or "#ff6060", thickness = 2 })
            end
        end
    end
end

function hud.fade(color: string, alpha: number): string
    local digits = string.match(color, "^#(%x%x%x%x%x%x)")
    return "#" .. (digits or "ffffff") .. string.format("%02x", math.floor(math.clamp(alpha, 0, 1) * 255 + 0.5))
end

local AXES = {
    { vector = dvec3.new(1, 0, 0), label = "X", color = "#ff5a5a" },
    { vector = dvec3.new(0, 1, 0), label = "Y", color = "#5aff7a" },
    { vector = dvec3.new(0, 0, 1), label = "Z", color = "#5a8aff" },
}

function hud.triad(h, cx: number, cy: number, size: number, att, text_size: number?)
    for _, axis in AXES do
        local px, py = dvec3.dot(axis.vector, att.right), dvec3.dot(axis.vector, att.up)
        local away = dvec3.dot(axis.vector, att.forward) > 0
        local color = away and hud.fade(axis.color, 0.45) or axis.color
        local ex, ey = cx + px * size, cy - py * size
        h.line(cx, cy, ex, ey, { color = color, thickness = 2 })
        h.text(ex + px * size * 0.25, ey - py * size * 0.25, axis.label, { size = text_size or 12, align = "center", color = color })
    end
end

return hud
