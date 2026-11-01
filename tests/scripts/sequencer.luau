local sequencer = require("lib/sequencer")
local plain = require("lib/plain")

local S = sequencer

local function recorder()
    local log = {}
    local state = S.new()
    local handlers = setmetatable({}, { __index = function(_, name)
        return function(...)
            table.insert(log, { name, state.time, ... })
        end
    end })
    return state, handlers, log
end

local function names(log)
    local out = {}
    for _, entry in log do table.insert(out, entry[1]) end
    return out
end

local function run(state, handlers, ticks, dt)
    for i = 1, ticks do S.update(state, handlers, dt) end
end

test("a call at an absolute time fires on the first update that reaches it", function()
    local state, handlers, log = recorder()
    S.at(state, 1, S.call("ping", 7, "x"))
    run(state, handlers, 3, 0.25)
    assert_eq(#log, 0)
    run(state, handlers, 1, 0.25)
    assert_eq(log, { { "ping", 1, 7, "x" } })
    run(state, handlers, 10, 0.25)
    assert_eq(#log, 1)
    assert_eq(S.active(state), 0)
end)

test("a call at time zero fires on the first update", function()
    local state, handlers, log = recorder()
    S.add(state, S.call("now"))
    S.update(state, handlers, 0.5)
    assert_eq(names(log), { "now" })
end)

test("relative offsets count from the moment of adding", function()
    local state, handlers, log = recorder()
    run(state, handlers, 4, 0.25)
    S.after(state, 0.5, S.call("later"))
    run(state, handlers, 1, 0.25)
    assert_eq(#log, 0)
    run(state, handlers, 1, 0.25)
    assert_eq(log, { { "later", 1.5 } })
    S.add(state, S.call("past"), { at = 0.1 })
    S.update(state, handlers, 0.25)
    assert_eq(names(log), { "later", "past" })
end)

test("a sequence runs steps one after another", function()
    local state, handlers, log = recorder()
    S.add(state, S.sequence({ S.call("a"), S.wait(1), S.call("b"), S.wait(0.5), S.call("c") }))
    run(state, handlers, 8, 0.25)
    assert_eq(log, { { "a", 0.25 }, { "b", 1 }, { "c", 1.5 } })
    assert_eq(S.active(state), 0)
end)

test("calls inside a sequence fire in order on a long frame", function()
    local state, handlers, log = recorder()
    S.add(state, S.sequence({ S.call("a"), S.wait(1), S.call("b"), S.wait(0), S.call("c"), S.wait(2), S.call("d") }))
    S.update(state, handlers, 100)
    assert_eq(names(log), { "a", "b", "c", "d" })
end)

test("parallel steps start together and finish with the longest", function()
    local state, handlers, log = recorder()
    S.add(state, S.parallel({
        S.sequence({ S.wait(1), S.call("slow") }),
        S.sequence({ S.wait(0.5), S.call("fast") }),
        S.call("now"),
    }))
    run(state, handlers, 1, 0.25)
    assert_eq(names(log), { "now" })
    assert_eq(S.active(state), 1)
    run(state, handlers, 1, 0.25)
    assert_eq(names(log), { "now", "fast" })
    run(state, handlers, 1, 0.25)
    assert_eq(names(log), { "now", "fast" })
    run(state, handlers, 1, 0.25)
    assert_eq(names(log), { "now", "fast", "slow" })
    assert_eq(S.active(state), 0)
end)

test("a tween samples the eased value and ends exactly on the target", function()
    local state, handlers, log = recorder()
    S.add(state, S.tween("fade", 10, 20, 1, "in_quad"))
    run(state, handlers, 6, 0.25)
    assert_eq(#log, 4)
    assert_near(log[1][3], 10 + 10 * 0.0625, 1e-12)
    assert_near(log[2][3], 10 + 10 * 0.25, 1e-12)
    assert_eq(log[4][3], 20)
    assert_eq(log[4][4], 1)
    assert_eq(S.active(state), 0)
end)

test("tweens work on vectors and zero-length tweens fire once", function()
    local state, handlers, log = recorder()
    S.add(state, S.tween("move", vector.create(0, 0, 0), vector.create(2, 4, 6), 1))
    S.add(state, S.tween("snap", 1, 9, 0))
    run(state, handlers, 2, 0.5)
    assert_eq(names(log), { "move", "snap", "move" })
    assert_eq(log[1][3], vector.create(1, 2, 3))
    assert_eq(log[2][3], 9)
    assert_eq(log[3][3], vector.create(2, 4, 6))
end)

test("a loop repeats a fixed number of times", function()
    local state, handlers, log = recorder()
    S.add(state, S.loop(S.sequence({ S.call("tick"), S.wait(1) }), 3))
    run(state, handlers, 20, 0.25)
    assert_eq(names(log), { "tick", "tick", "tick" })
    assert_eq(S.active(state), 0)
    S.add(state, S.loop(S.wait(1), 0))
    S.update(state, handlers, 1)
    assert_eq(S.active(state), 0)
end)

test("every fires at the period and keeps going until cancelled", function()
    local state, handlers, log = recorder()
    local id = S.add(state, S.every(0.5, "beat"))
    run(state, handlers, 40, 0.25)
    assert_eq(#log, 21)
    assert_eq(S.active(state), 1)
    assert_eq(S.cancel(state, id), 1)
    run(state, handlers, 40, 0.25)
    assert_eq(#log, 21)
end)

test("a tween in a loop reaches its end every iteration", function()
    local state, handlers, log = recorder()
    S.add(state, S.loop(S.tween("pulse", 0, 1, 0.1), 5))
    run(state, handlers, 100, 1 / 64)
    local finished = 0
    for _, entry in log do
        if entry[4] == 1 then finished += 1 end
    end
    assert_eq(finished, 5)
    assert_eq(S.active(state), 0)
end)

test("nested loops and groups keep exact counts across tick sizes", function()
    local body = S.loop(S.parallel({ S.call("inner"), S.sequence({ S.wait(0.125), S.call("half") }) }), 4)
    for _, dt in { 1 / 64, 1 / 16, 1 / 7, 1 / 3 } do
        local state, handlers, log = recorder()
        S.add(state, S.sequence({ S.loop(S.sequence({ body, S.wait(0.25) }), 3), S.call("done") }))
        run(state, handlers, math.ceil(20 / dt), dt)
        local counts = {}
        for _, name in names(log) do counts[name] = (counts[name] or 0) + 1 end
        assert_eq(counts, { inner = 12, half = 12, done = 1 }, "dt " .. dt)
        assert_eq(log[#log][1], "done")
    end
end)

test("event order does not depend on the tick size", function()
    for _, dt in { 1 / 64, 1 / 16, 1 / 4 } do
        local state, handlers, log = recorder()
        S.add(state, S.sequence({ S.call("a"), S.wait(0.5), S.parallel({ S.call("b"), S.sequence({ S.wait(0.25), S.call("c") }) }), S.wait(0.25), S.call("d") }))
        S.at(state, 0.75, S.call("e"))
        run(state, handlers, math.ceil(2 / dt), dt)
        assert_eq(names(log), { "a", "b", "c", "e", "d" }, "dt " .. dt)
    end
end)

test("cancel by id or tag, also from inside a handler", function()
    local state = S.new()
    local log = {}
    local handlers = {
        stop = function() S.cancel(state, "group") end,
        a = function() table.insert(log, "a") end,
        b = function() table.insert(log, "b") end,
    }
    S.add(state, S.every(1, "a"), { tag = "group" })
    S.add(state, S.every(1, "b"), { tag = "group" })
    S.at(state, 2.5, S.call("stop"))
    local keep = S.add(state, S.sequence({ S.wait(10), S.call("a") }))
    run(state, handlers, 16, 0.25)
    assert_eq(log, { "a", "b", "a", "b", "a", "b" })
    assert_eq(S.active(state), 1)
    assert_eq(S.cancel(state, 12345), 0)
    assert_eq(S.cancel(state, keep), 1)
    assert_eq(S.active(state), 0)
    S.add(state, S.call("a"))
    S.clear(state)
    assert_eq(S.active(state), 0)
end)

test("handlers may schedule more steps", function()
    local state = S.new()
    local log = {}
    local handlers
    handlers = {
        first = function() table.insert(log, "first") S.after(state, 0.5, S.call("second")) end,
        second = function() table.insert(log, "second") end,
    }
    S.add(state, S.call("first"))
    run(state, handlers, 4, 0.25)
    assert_eq(log, { "first", "second" })
end)

test("invalid steps and missing handlers fail loudly", function()
    expect_error(function() S.wait(-1) end, "duration must be a number >= 0")
    expect_error(function() S.tween("x", 0, 1, -2) end, "duration")
    expect_error(function() S.tween("x", 0, 1, 1, "bounce") end, "unknown easing")
    expect_error(function() S.loop(S.call("x")) end, "must take time")
    expect_error(function() S.loop(S.wait(1), 1.5) end, "whole number")
    local state = S.new()
    S.add(state, S.call("ghost"))
    expect_error(function() S.update(state, {}, 1) end, "no handler 'ghost'")
end)

test("the deepest allowed nesting stays plain and deeper is rejected", function()
    local node = S.call("leaf", "arg")
    for i = 1, 7 do node = S.sequence({ S.wait(0.1), node }) end
    local state = S.new()
    S.add(state, node)
    assert(plain.check(state))
    S.update(state, { leaf = function() end }, 10)
    node = S.sequence({ node })
    expect_error(function() S.add(S.new(), node) end, "levels deep")
end)

local function scenario()
    local state = S.new()
    S.add(state, S.sequence({
        S.call("go", vector.create(1, 2, 3), true, "text"),
        S.tween("ramp", 0, 5, 0.75, "smoothstep"),
        S.parallel({ S.every(0.25, "beat"), S.sequence({ S.wait(1), S.call("stop") }) }),
    }), { tag = "main" })
    S.after(state, 0.3, S.tween("side", vector.create(0, 0, 0), vector.create(9, 9, 9), 2))
    return state
end

local function drive(state, ticks, log)
    local handlers = setmetatable({}, { __index = function(_, name)
        return function(...) table.insert(log, { name, state.time, ... }) end
    end })
    for i = 1, ticks do
        S.update(state, handlers, 1 / 64)
        if log[#log] and log[#log][1] == "stop" then S.cancel(state, "main") end
    end
end

test("state is plain data and a copy continues identically", function()
    local state = scenario()
    assert(plain.check(state))
    local log = {}
    drive(state, 40, log)
    local ok, where = plain.check(state)
    assert(ok, where)
    local copy = plain.copy(state)
    local a, b = {}, {}
    drive(state, 200, a)
    drive(copy, 200, b)
    assert_eq(a, b)
    assert(#a > 10)
    assert_eq(state, copy)
end)

test("replaying from scratch gives the same log", function()
    local first, second = {}, {}
    drive(scenario(), 300, first)
    drive(scenario(), 300, second)
    assert_eq(first, second)
end)

local PROBE = [[
local sequencer = require("lib/sequencer")
local plain = require("lib/plain")
return {
    OnStart = function(self)
        local s = sequencer
        self.seq = s.new()
        s.add(self.seq, s.sequence({
            s.call("emit", "start"),
            s.tween("emit_value", 0, 1, 0.5),
            s.every(0.125, "emit", "beat"),
        }))
    end,
    OnTick = function(self, dt)
        self.n = (self.n or 0) + 1
        if self.n == 20 and scene.name(self.entity) == "B" then
            self.seq = plain.copy(self.seq)
        end
        local who = scene.name(self.entity)
        sequencer.update(self.seq, {
            emit = function(what) events.emit("cue", who, what) end,
            emit_value = function(value) events.emit("cue", who, "value", value) end,
        }, dt)
    end,
}
]]

test("a sequencer script driven by the sim clock replays identically", function()
    sim.define_script("seq_probe", PROBE)
    sim.set_dt(1 / 64)
    local a, b = sim.spawn("A"), sim.spawn("B")
    sim.attach(a, "seq_probe")
    sim.attach(b, "seq_probe")
    sim.step(64)
    local per = { A = {}, B = {} }
    for _, event in sim.events("cue") do
        table.insert(per[event.args[1]], { event.tick, event.args[2], event.args[3] })
    end
    assert(#per.A > 10)
    assert_eq(per.A, per.B)
    assert_eq(per.A[1], { 0, "start", nil })
    local value
    for _, entry in per.A do
        if entry[2] == "value" then value = entry[3] end
    end
    assert_eq(value, 1)
end)
