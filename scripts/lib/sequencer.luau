local curves = require("lib/curves")

local sequencer = {}

local MAX_DEPTH = 7

local function check_duration(duration: number, name: string)
    if type(duration) ~= "number" or duration < 0 or duration ~= duration then
        error("sequencer." .. name .. ": duration must be a number >= 0", 3)
    end
end

function sequencer.wait(duration: number)
    check_duration(duration, "wait")
    return { kind = "wait", dur = duration, depth = 0 }
end

function sequencer.call(name: string, ...: any)
    local node = { ... }
    node.kind, node.name, node.dur, node.depth = "call", name, 0, 0
    return node
end

function sequencer.tween(name: string, from: any, to: any, duration: number, ease: string?)
    check_duration(duration, "tween")
    curves.ease(ease or "linear", 0)
    return { kind = "tween", name = name, from = from, to = to, ease = ease or "linear", dur = duration, depth = 0 }
end

local function group(kind: string, children: { any }, total: (number, number) -> number)
    local node = table.clone(children)
    local dur, depth = 0, 0
    for _, child in children do
        dur = total(dur, child.dur)
        depth = math.max(depth, child.depth + 1)
    end
    node.kind, node.dur, node.depth = kind, dur, depth
    return node
end

function sequencer.sequence(children: { any })
    return group("seq", children, function(sum, d) return sum + d end)
end

function sequencer.parallel(children: { any })
    return group("par", children, math.max)
end

function sequencer.loop(child: any, count: number?)
    if child.dur <= 0 then
        error("sequencer.loop: the body must take time", 2)
    end
    if count ~= nil and (count < 0 or count ~= math.floor(count)) then
        error("sequencer.loop: count must be a whole number >= 0", 2)
    end
    return { kind = "loop", child = child, count = count, dur = count and child.dur * count or math.huge, depth = child.depth + 1 }
end

function sequencer.every(period: number, name: string, ...: any)
    return sequencer.loop(sequencer.sequence({ sequencer.call(name, ...), sequencer.wait(period) }))
end

local function handler(handlers, name: string)
    local fn = handlers[name]
    if not fn then
        error("sequencer: no handler '" .. tostring(name) .. "'", 0)
    end
    return fn
end

local function rebase(t: number, start: number, finish: number, dur: number): number
    return t >= finish and dur or t - start
end

local function fire(node, p: number, q: number, handlers)
    local kind = node.kind
    if kind == "call" then
        if p < 0 and q >= 0 then
            handler(handlers, node.name)(table.unpack(node))
        end
    elseif kind == "tween" then
        if q >= 0 and p < node.dur then
            local u = node.dur > 0 and math.min(q / node.dur, 1) or 1
            handler(handlers, node.name)(curves.lerp(node.from, node.to, curves.ease(node.ease, u)), u)
        end
    elseif kind == "seq" then
        local start = 0
        for i = 1, #node do
            local child = node[i]
            if start > q then
                break
            end
            local finish = start + child.dur
            if q >= start and p < finish then
                fire(child, rebase(p, start, finish, child.dur), rebase(q, start, finish, child.dur), handlers)
            end
            start = finish
        end
    elseif kind == "par" then
        for i = 1, #node do
            local child = node[i]
            if q >= 0 and p < child.dur then
                fire(child, rebase(p, 0, child.dur, child.dur), rebase(q, 0, child.dur, child.dur), handlers)
            end
        end
    elseif kind == "loop" then
        local body = node.child.dur
        local last = node.count and node.count - 1 or math.huge
        for k = math.max(0, math.ceil(p / body) - 1), math.min(last, math.floor(q / body)) do
            local finish = (k + 1) * body
            fire(node.child, rebase(p, k * body, finish, body), rebase(q, k * body, finish, body), handlers)
        end
    end
end

function sequencer.new()
    return { time = 0, next_id = 1, cues = {}, busy = false }
end

function sequencer.add(state, node, options: { at: number?, after: number?, tag: string? }?): number
    if node.depth > MAX_DEPTH then
        error("sequencer.add: nested " .. node.depth .. " levels deep, the limit is " .. MAX_DEPTH, 2)
    end
    local opts = options or {}
    local id = state.next_id
    state.next_id += 1
    table.insert(state.cues, {
        id = id,
        tag = opts.tag,
        start = opts.at or state.time + (opts.after or 0),
        prev = -1,
        node = node,
        dead = false,
    })
    return id
end

function sequencer.at(state, time_: number, node, tag: string?): number
    return sequencer.add(state, node, { at = time_, tag = tag })
end

function sequencer.after(state, delay: number, node, tag: string?): number
    return sequencer.add(state, node, { after = delay, tag = tag })
end

local function compact(state)
    local kept = {}
    for _, cue in state.cues do
        if not cue.dead then
            table.insert(kept, cue)
        end
    end
    state.cues = kept
end

function sequencer.cancel(state, key: number | string): number
    local removed = 0
    for _, cue in state.cues do
        if not cue.dead and (cue.id == key or cue.tag == key) then
            cue.dead = true
            removed += 1
        end
    end
    if not state.busy then
        compact(state)
    end
    return removed
end

function sequencer.clear(state)
    for _, cue in state.cues do
        cue.dead = true
    end
    if not state.busy then
        compact(state)
    end
end

function sequencer.active(state): number
    local count = 0
    for _, cue in state.cues do
        if not cue.dead then
            count += 1
        end
    end
    return count
end

function sequencer.update(state, handlers, dt: number)
    state.time += dt
    state.busy = true
    local i = 1
    while i <= #state.cues do
        local cue = state.cues[i]
        local q = state.time - cue.start
        if not cue.dead and q >= 0 then
            fire(cue.node, cue.prev, q, handlers)
            cue.prev = q
            if q >= cue.node.dur then
                cue.dead = true
            end
        end
        i += 1
    end
    state.busy = false
    compact(state)
end

return sequencer
