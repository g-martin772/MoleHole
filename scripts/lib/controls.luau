local hud = require("lib/hud")

local controls = {}

controls.AUTO_SECONDS = 10

controls.groups = {
    {
        title = "THRUST",
        rows = {
            { action = "move_z", label = "Forward / back" },
            { action = "move_x", label = "Strafe" },
            { action = "move_y", label = "Up / down" },
            { action = "boost", label = "Boost" },
        },
    },
    {
        title = "ROTATION",
        rows = {
            { action = "pitch", label = "Pitch" },
            { action = "yaw", label = "Yaw" },
            { action = "roll", label = "Roll" },
        },
    },
    {
        title = "THROTTLE",
        rows = {
            { action = "throttle", label = "Throttle up / down" },
            { action = "throttle_cut", label = "Throttle to zero" },
            { action = "speed_hold", label = "Hold speed" },
            { action = "kill_velocity", label = "Kill velocity" },
        },
    },
    {
        title = "ASSISTS",
        rows = {
            { action = "assist", label = "Cycle assist" },
            { action = "control_mode", label = "Mouse steer on / off" },
        },
    },
    {
        title = "CAMERA",
        rows = {
            { action = "camera", label = "Cycle camera" },
            { action = "cam_yaw", label = "Orbit camera" },
            { action = "cam_pitch", label = "Tilt camera" },
        },
    },
    {
        title = "HUD / AUTOPILOT",
        rows = {
            { action = "hud", label = "Cycle HUD" },
            { action = "autopilot", label = "Autopilot on / off" },
            { action = "autopilot_mode", label = "Autopilot mode" },
            { action = "controls", label = "This panel" },
        },
    },
}

local KEY_NAMES = {
    space = "Space", ctrl = "Ctrl", shift = "Shift", alt = "Alt", enter = "Enter", tab = "Tab", backspace = "Bksp",
    up = "Up", down = "Down", left = "Left", right = "Right",
    comma = ",", period = ".", slash = "/", semicolon = ";", apostrophe = "'", leftbracket = "[", rightbracket = "]",
    minus = "-", equals = "=", grave = "`", pageup = "PgUp", pagedown = "PgDn", home = "Home", ["end"] = "End",
    insert = "Ins", delete = "Del",
}

local BUTTON_NAMES = { left = "LMB", middle = "MMB", right = "RMB", x1 = "Mouse4", x2 = "Mouse5" }

local WHITE, DIM, GOLD, CYAN = "#e8f0ff", "#9fb0c8", "#ffc040", "#60d8ff"

function controls.key_label(source: string): string
    local kind, name = string.match(source, "^(%a+):(.+)$")
    if kind == "mouse" then
        return BUTTON_NAMES[name] or string.upper(name)
    end
    return KEY_NAMES[name or source] or string.upper(name or source)
end

local function split_sign(binding: string): (string, number)
    local scale, source = string.match(binding, "^([%-%d%.eE+]+)%*(.+)$")
    if scale ~= nil then
        return source, (tonumber(scale) or 1) < 0 and -1 or 1
    end
    if string.sub(binding, 1, 1) == "-" then
        return string.sub(binding, 2), -1
    end
    return binding, 1
end

function controls.describe(bindings: { string }): string?
    local positive, negative = {}, {}
    for _, binding in bindings do
        local source, sign = split_sign(binding)
        table.insert(sign < 0 and negative or positive, controls.key_label(source))
    end
    if #positive == 0 and #negative == 0 then
        return nil
    end
    if #negative == 0 then
        return table.concat(positive, ", ")
    elseif #positive == 0 then
        return "- " .. table.concat(negative, ", ")
    end
    return table.concat(positive, ", ") .. " / " .. table.concat(negative, ", ")
end

function controls.read(action: string): string?
    return controls.describe({ input.bindings(action) })
end

function controls.collect(reader: ((string) -> string?)?)
    local read = reader or controls.read
    local shown = {}
    for _, group in controls.groups do
        local rows = {}
        for _, row in group.rows do
            local keys = read(row.action)
            if keys ~= nil then
                table.insert(rows, { label = row.label, keys = keys, action = row.action })
            end
        end
        if #rows > 0 then
            table.insert(shown, { title = group.title, rows = rows })
        end
    end
    return shown
end

local function column_split(shown, columns: number)
    local total = 0
    for _, group in shown do
        total += #group.rows + 1
    end
    local target = math.ceil(total / columns)
    local split, current, used = { {} }, 1, 0
    for _, group in shown do
        local lines = #group.rows + 1
        if used > 0 and used + lines > target and current < columns then
            current += 1
            split[current] = {}
            used = 0
        end
        table.insert(split[current], group)
        used += lines
    end
    return split
end

function controls.new()
    return { was = false, seen = false, mode = "auto", since = 0 }
end

local function showing(state, captured: boolean?, now: number): boolean
    if captured ~= true then
        return false
    end
    return state.mode == "open" or (state.mode == "auto" and now - state.since < controls.AUTO_SECONDS)
end

function controls.update(state, captured: boolean?, toggle: boolean, now: number): boolean
    if captured == true and not state.was and not state.seen then
        state.seen, state.mode, state.since = true, "auto", now
    end
    state.was = captured == true
    if toggle and captured == true then
        state.mode = showing(state, captured, now) and "closed" or "open"
    end
    return showing(state, captured, now)
end

function controls.draw_hint(h, text: string, size: number, y: number, color: string?)
    local width = #text * size * 0.00077 + 0.06
    local height = size / 720 * 1.35 + 0.03
    h.rect(-width / 2, y, width, height, { color = hud.fade("#05080f", 0.82), filled = true })
    h.rect(-width / 2, y, width, height, { color = hud.fade(color or GOLD, 0.8), thickness = 1 })
    h.text(0, y + 0.015, text, { size = size, color = color or WHITE, align = "center" })
end

function controls.draw(h, shown, options)
    if #shown == 0 then
        return
    end
    local size = options.size
    local step = size / 720 * 1.35
    local col_w = 0.40 * size / 15
    local pad = 0.02
    local columns = math.clamp(math.floor((options.aspect * 0.94 - 2 * pad) / col_w), 1, 3)
    local split = column_split(shown, columns)
    local rows = 0
    for _, column in split do
        local lines = 0
        for _, group in column do
            lines += #group.rows + 1
        end
        rows = math.max(rows, lines)
    end
    local width = #split * col_w + 2 * pad
    local height = (rows + 2) * step + 2 * pad
    local x0, y0 = -width / 2, options.top
    h.rect(x0, y0, width, height, { color = hud.fade("#05080f", 0.82), filled = true })
    h.rect(x0, y0, width, height, { color = hud.fade(CYAN, 0.7), thickness = 1 })
    for index, column in split do
        local left = x0 + pad + (index - 1) * col_w
        local right = left + col_w - 0.02
        local y = y0 + pad
        for _, group in column do
            h.text(left, y, group.title, { size = size * 0.85, color = GOLD })
            y += step
            for _, row in group.rows do
                h.text(left, y, row.label, { size = size, color = DIM })
                h.text(right, y, row.keys, { size = size, color = WHITE, align = "right" })
                y += step
            end
        end
    end
    h.text(0, y0 + height - pad - step, "Esc releases the mouse and keys", { size = size * 0.85, color = DIM, align = "center" })
end

return controls
