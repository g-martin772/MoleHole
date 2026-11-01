local curves = {}

local easings = {
    linear = function(t) return t end,
    smoothstep = function(t) return t * t * (3 - 2 * t) end,
    smootherstep = function(t) return t * t * t * (t * (t * 6 - 15) + 10) end,
    in_quad = function(t) return t * t end,
    out_quad = function(t) return 1 - (1 - t) * (1 - t) end,
    in_out_quad = function(t) return t < 0.5 and 2 * t * t or 1 - 2 * (1 - t) * (1 - t) end,
    in_cubic = function(t) return t * t * t end,
    out_cubic = function(t) local u = 1 - t return 1 - u * u * u end,
    in_out_cubic = function(t) local u = 1 - t return t < 0.5 and 4 * t * t * t or 1 - 4 * u * u * u end,
    in_sine = function(t) return 1 - math.cos(t * math.pi / 2) end,
    out_sine = function(t) return math.sin(t * math.pi / 2) end,
    in_out_sine = function(t) return (1 - math.cos(math.pi * t)) / 2 end,
}
easings.easeIn = easings.in_quad
easings.easeOut = easings.out_quad
easings.easeInOut = easings.in_out_quad
easings.cubicIn = easings.in_cubic
easings.cubicOut = easings.out_cubic
easings.cubicInOut = easings.in_out_cubic

curves.easings = easings

function curves.ease(name: string, t: number): number
    local fn = easings[name]
    if not fn then
        error("curves.ease: unknown easing '" .. tostring(name) .. "'", 2)
    end
    return fn(math.clamp(t, 0, 1))
end

local function magnitude(v)
    local kind = typeof(v)
    if kind == "number" then
        return math.abs(v)
    elseif kind == "vector" then
        return vector.magnitude(v)
    end
    return dvec3.length(v)
end

local function dot(a, b)
    local kind = typeof(a)
    if kind == "number" then
        return a * b
    elseif kind == "vector" then
        return vector.dot(a, b)
    end
    return dvec3.dot(a, b)
end

function curves.zero_like(v)
    local kind = typeof(v)
    if kind == "number" then
        return 0
    elseif kind == "vector" then
        return vector.zero
    end
    return dvec3.new(0, 0, 0)
end

function curves.clamp01(t: number): number
    return math.clamp(t, 0, 1)
end

function curves.lerp(a, b, t: number)
    if typeof(a) == "dquat" then
        return dquat.slerp(a, b, t)
    end
    return a + (b - a) * t
end

function curves.inverse_lerp(a: number, b: number, value: number): number
    if a == b then
        return 0
    end
    return (value - a) / (b - a)
end

function curves.remap(value: number, in_min: number, in_max: number, out_min: number, out_max: number): number
    return out_min + (out_max - out_min) * curves.inverse_lerp(in_min, in_max, value)
end

function curves.bezier(points: { any }, t: number)
    local n = #points
    if n == 0 then
        error("curves.bezier: no control points", 2)
    end
    local work = table.clone(points)
    for level = n - 1, 1, -1 do
        for i = 1, level do
            work[i] = work[i] + (work[i + 1] - work[i]) * t
        end
    end
    return work[1]
end

function curves.bezier_tangent(points: { any }, t: number)
    local n = #points
    if n < 2 then
        error("curves.bezier_tangent: needs at least 2 control points", 2)
    end
    local differences = {}
    for i = 1, n - 1 do
        differences[i] = (points[i + 1] - points[i]) * (n - 1)
    end
    return curves.bezier(differences, t)
end

function curves.hermite(p0, m0, p1, m1, t: number)
    local t2 = t * t
    local t3 = t2 * t
    return p0 + (p1 - p0) * (3 * t2 - 2 * t3) + m0 * (t3 - 2 * t2 + t) + m1 * (t3 - t2)
end

function curves.hermite_tangent(p0, m0, p1, m1, t: number)
    local t2 = t * t
    return (p1 - p0) * (6 * t - 6 * t2) + m0 * (3 * t2 - 4 * t + 1) + m1 * (3 * t2 - 2 * t)
end

function curves.spline(points: { any }, options: { closed: boolean?, tension: number? }?)
    if #points < 2 then
        error("curves.spline: needs at least 2 points", 2)
    end
    local opts = options or {}
    return { points = points, closed = opts.closed == true, tension = opts.tension or 0 }
end

function curves.spline_segments(spline): number
    local n = #spline.points
    return spline.closed and n or n - 1
end

local function point_tangent(spline, i: number)
    local p, n = spline.points, #spline.points
    local keep = 1 - spline.tension
    if spline.closed then
        return (p[i % n + 1] - p[(i - 2) % n + 1]) * (keep * 0.5)
    elseif i == 1 then
        return (p[2] - p[1]) * keep
    elseif i == n then
        return (p[n] - p[n - 1]) * keep
    end
    return (p[i + 1] - p[i - 1]) * (keep * 0.5)
end

local function locate(spline, u: number): (number, number, number)
    local segments = curves.spline_segments(spline)
    if spline.closed then
        u = u % segments
    else
        u = math.clamp(u, 0, segments)
    end
    local i = math.min(math.floor(u), segments - 1)
    local n = #spline.points
    return i + 1, spline.closed and (i + 1) % n + 1 or i + 2, u - i
end

function curves.spline_eval(spline, u: number)
    local i, j, f = locate(spline, u)
    local p = spline.points
    return curves.hermite(p[i], point_tangent(spline, i), p[j], point_tangent(spline, j), f)
end

function curves.spline_tangent(spline, u: number)
    local i, j, f = locate(spline, u)
    local p = spline.points
    return curves.hermite_tangent(p[i], point_tangent(spline, i), p[j], point_tangent(spline, j), f)
end

function curves.arc_table(fn: (number) -> any, u0: number, u1: number, samples: number?)
    local count = samples or 64
    local us, ss = { u0 }, { 0 }
    local previous = fn(u0)
    local total = 0
    for i = 1, count do
        local u = u0 + (u1 - u0) * (i / count)
        local point = fn(u)
        total += magnitude(point - previous)
        us[i + 1] = u
        ss[i + 1] = total
        previous = point
    end
    return { u = us, s = ss, length = total }
end

function curves.u_at_length(arc: { u: { number }, s: { number }, length: number }, distance: number): number
    local us, ss = arc.u, arc.s
    local count = #us
    if distance <= 0 then
        return us[1]
    elseif distance >= arc.length then
        return us[count]
    end
    local lo, hi = 1, count
    while hi - lo > 1 do
        local mid = (lo + hi) // 2
        if ss[mid] <= distance then
            lo = mid
        else
            hi = mid
        end
    end
    local span = ss[hi] - ss[lo]
    local f = span > 0 and (distance - ss[lo]) / span or 0
    return us[lo] + (us[hi] - us[lo]) * f
end

function curves.length_at_u(arc: { u: { number }, s: { number }, length: number }, u: number): number
    local us, ss = arc.u, arc.s
    local count = #us
    if u <= us[1] then
        return 0
    elseif u >= us[count] then
        return arc.length
    end
    local lo, hi = 1, count
    while hi - lo > 1 do
        local mid = (lo + hi) // 2
        if us[mid] <= u then
            lo = mid
        else
            hi = mid
        end
    end
    local f = (u - us[lo]) / (us[hi] - us[lo])
    return ss[lo] + (ss[hi] - ss[lo]) * f
end

function curves.spline_table(spline, per_segment: number?)
    local segments = curves.spline_segments(spline)
    return curves.arc_table(function(u) return curves.spline_eval(spline, u) end, 0, segments, segments * (per_segment or 32))
end

function curves.spline_at_length(spline, arc, distance: number)
    if spline.closed and arc.length > 0 then
        distance = distance % arc.length
    end
    return curves.spline_eval(spline, curves.u_at_length(arc, distance))
end

function curves.track(keys: { { t: number, v: any, mode: string? } }, options: { wrap: string? }?)
    local n = #keys
    if n == 0 then
        error("curves.track: no keys", 2)
    end
    for i = 2, n do
        if keys[i].t <= keys[i - 1].t then
            error("curves.track: key times must increase (key " .. i .. ")", 2)
        end
    end
    return { keys = keys, wrap = options and options.wrap or "clamp" }
end

function curves.track_duration(track): number
    local keys = track.keys
    return keys[#keys].t - keys[1].t
end

local function key_slope(keys, i: number)
    local n = #keys
    local a, b = keys[math.max(i - 1, 1)], keys[math.min(i + 1, n)]
    return (b.v - a.v) / (b.t - a.t)
end

function curves.track_eval(track, t: number)
    local keys = track.keys
    local n = #keys
    local first, last = keys[1].t, keys[n].t
    local span = last - first
    if span > 0 then
        if track.wrap == "loop" then
            t = first + (t - first) % span
        elseif track.wrap == "pingpong" then
            local m = (t - first) % (2 * span)
            t = first + (m > span and 2 * span - m or m)
        end
    end
    if t <= first then
        return keys[1].v
    elseif t >= last then
        return keys[n].v
    end
    local lo, hi = 1, n
    while hi - lo > 1 do
        local mid = (lo + hi) // 2
        if keys[mid].t <= t then
            lo = mid
        else
            hi = mid
        end
    end
    local a, b = keys[lo], keys[hi]
    local dt = b.t - a.t
    local u = (t - a.t) / dt
    local mode = a.mode or "linear"
    if mode == "step" then
        return a.v
    elseif mode == "cubic" and typeof(a.v) ~= "dquat" then
        return curves.hermite(a.v, key_slope(keys, lo) * dt, b.v, key_slope(keys, hi) * dt, u)
    end
    return curves.lerp(a.v, b.v, curves.ease(mode == "cubic" and "linear" or mode, u))
end

function curves.damp(current, target, lambda: number, dt: number)
    return curves.lerp(current, target, 1 - math.exp(-lambda * dt))
end

function curves.smooth_damp(current, target, velocity, smooth_time: number, dt: number, max_speed: number?)
    if dt <= 0 then
        return current, velocity
    end
    smooth_time = math.max(smooth_time, 1e-4)
    local omega = 2 / smooth_time
    local x = omega * dt
    local decay = 1 / (1 + x + 0.48 * x * x + 0.235 * x * x * x)
    local change = current - target
    if max_speed then
        local max_change = max_speed * smooth_time
        local length = magnitude(change)
        if length > max_change then
            change = change * (max_change / length)
        end
    end
    local reachable = current - change
    local carry = (velocity + change * omega) * dt
    local output = reachable + (change + carry) * decay
    if dot(target - current, output - target) > 0 then
        return target, curves.zero_like(target)
    end
    return output, (velocity - carry * omega) * decay
end

return curves
