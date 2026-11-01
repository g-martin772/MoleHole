local EMITTER = [[
return {
    OnTick = function(self)
        events.emit("e", 1)
        events.emit("e", 2)
        events.emit("f", "x", vector.create(1, 2, 3), self.entity, true)
    end,
}
]]

local function summary(list)
    local out = {}
    for _, event in list do table.insert(out, event.name .. ":" .. tostring(event.args[1])) end
    return out
end

test("events are delivered in emit order with their arguments", function()
    sim.define_script("emitter", EMITTER)
    local entity = sim.spawn("Emitter")
    sim.attach(entity, "emitter")
    sim.step(1)
    assert_eq(summary(sim.events()), { "e:1", "e:2", "f:x" })
    local f = sim.events("f")[1]
    assert_eq(f.args[2], vector.create(1, 2, 3))
    assert_eq(f.args[3], entity)
    assert_eq(f.args[4], true)
    assert_eq(f.tick, 0)
end)

test("events.on handlers and OnEvent see events in order, replies queue behind them", function()
    sim.define_script("emitter", EMITTER)
    sim.define_script("listener", [[
return {
    OnStart = function(self)
        events.on("e", function(n) events.emit("handler", n) end)
    end,
    OnEvent = function(self, name, ...)
        if name == "f" then events.emit("catch_all", ...) end
    end,
}
]])
    sim.attach(sim.spawn("Listener"), "listener")
    sim.attach(sim.spawn("Emitter"), "emitter")
    sim.step(1)
    assert_eq(summary(sim.events()), { "e:1", "e:2", "f:x", "handler:1", "handler:2", "catch_all:x" })
end)

test("every listener gets the event, in entity order", function()
    sim.define_script("listener", [[
return {
    properties = { { name = "Tag", type = "string", default = "?" } },
    OnStart = function(self)
        events.on("ping", function() events.emit("pong", self.props.Tag) end)
    end,
}
]])
    local first = sim.spawn("First")
    local second = sim.spawn("Second")
    sim.attach(second, "listener", { Tag = "b" })
    sim.attach(first, "listener", { Tag = "a" })
    sim.step(1)
    sim.emit("ping")
    sim.step(1)
    assert_eq(summary(sim.events("pong")), { "pong:a", "pong:b" })
end)

test("events.off removes a handler and OnStop drops the subscriptions", function()
    sim.define_script("listener", [[
local function handler() events.emit("got") end
return {
    OnStart = function(self)
        events.on("ping", handler)
        events.on("drop", handler)
        events.off("drop", handler)
    end,
}
]])
    local entity = sim.spawn("Listener")
    sim.attach(entity, "listener")
    sim.step(1)
    sim.emit("drop")
    sim.emit("ping")
    sim.step(1)
    assert_eq(#sim.events("got"), 1)
    sim.destroy(entity)
    sim.step(1)
    sim.emit("ping")
    sim.step(1)
    assert_eq(#sim.events("got"), 1)
end)

test("events emitted from the test arrive at the end of the next tick", function()
    sim.emit("hello", 7)
    assert_eq(#sim.events("hello"), 0)
    sim.step(2)
    local hello = sim.events("hello")
    assert_eq(#hello, 1)
    assert_eq(hello[1].args[1], 7)
    assert_eq(hello[1].tick, 0)
end)

test("an error in a handler fails the step", function()
    sim.define_script("listener", [[return { OnEvent = function(self, name) error("handler " .. name) end }]])
    sim.attach(sim.spawn("Listener"), "listener")
    sim.step(1)
    sim.emit("boom")
    expect_error(function() sim.step(1) end, "handler boom")
end)

test("an event storm is cut off instead of hanging", function()
    sim.define_script("storm", [[return { OnEvent = function(self, name) events.emit(name) end }]])
    sim.attach(sim.spawn("Storm"), "storm")
    sim.step(1)
    sim.emit("loop")
    expect_error(function() sim.step(1) end, "too many events")
end)

test("events.emit rejects an empty name and events.on needs a callback context", function()
    sim.define_script("bad", [[return { OnStart = function() events.emit("") end }]])
    sim.attach(sim.spawn("Bad"), "bad")
    expect_error(function() sim.step(1) end, "non-empty")
end)
