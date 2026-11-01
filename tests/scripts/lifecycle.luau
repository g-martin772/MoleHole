local PROBE = [[
return {
    OnStart = function(self) events.emit("log", "start") end,
    OnFixedTick = function(self, dt) events.emit("log", "fixed") end,
    OnTick = function(self, dt) events.emit("log", "tick") end,
    OnLateTick = function(self, dt) events.emit("log", "late") end,
    OnStop = function(self)
        events.emit("log", "stop")
        scene.set(self.entity, "Transform", "Position", vector.create(99, 0, 0))
    end,
}
]]

local function names(list)
    local out = {}
    for _, event in list do table.insert(out, event.args[1]) end
    return out
end

test("callbacks run in lifecycle order each tick", function()
    sim.define_script("probe", PROBE)
    sim.attach(sim.spawn("Probe"), "probe")
    sim.step(2)
    assert_eq(names(sim.events("log")), { "start", "fixed", "tick", "late", "fixed", "tick", "late" })
end)

test("OnStart runs once, before the first tick of a script attached later", function()
    sim.define_script("probe", PROBE)
    local entity = sim.spawn("Probe")
    sim.step(1)
    sim.attach(entity, "probe")
    sim.step(1)
    assert_eq(names(sim.events("log")), { "start", "fixed", "tick", "late" })
end)

test("scripts run in entity order whatever the attach order", function()
    sim.define_script("tagged", [[
return {
    properties = { { name = "Tag", type = "string", default = "?" } },
    OnFixedTick = function(self) events.emit("log", self.props.Tag .. ":fixed") end,
    OnTick = function(self) events.emit("log", self.props.Tag .. ":tick") end,
    OnLateTick = function(self) events.emit("log", self.props.Tag .. ":late") end,
}
]])
    local first = sim.spawn("First")
    local second = sim.spawn("Second")
    sim.attach(second, "tagged", { Tag = "b" })
    sim.attach(first, "tagged", { Tag = "a" })
    sim.step(1)
    assert_eq(names(sim.events("log")), { "a:fixed", "a:tick", "b:fixed", "b:tick", "a:late", "b:late" })
end)

test("OnStop runs when the simulation stops and its writes apply", function()
    sim.define_script("probe", PROBE)
    local entity = sim.spawn("Probe")
    sim.attach(entity, "probe")
    sim.step(1)
    assert_eq(#sim.events("log"), 4)
    sim.stop()
    local log = names(sim.events("log"))
    assert_eq(log[#log], "stop")
    assert_eq(scene.get(entity, "Transform", "Position"), vector.create(99, 0, 0))
    expect_error(function() sim.step(1) end, "stopped")
end)

test("OnStop runs when the entity is destroyed and a stopped script is not ticked again", function()
    sim.define_script("probe", PROBE)
    local entity = sim.spawn("Probe")
    sim.attach(entity, "probe")
    sim.step(1)
    sim.destroy(entity)
    sim.step(2)
    assert_eq(names(sim.events("log")), { "start", "fixed", "tick", "late", "stop" })
end)

test("a hot reload stops the old script and starts the new one", function()
    sim.define_script("probe", [[return {
        OnStart = function() events.emit("log", "v1 start") end,
        OnStop = function() events.emit("log", "v1 stop") end,
    }]])
    sim.attach(sim.spawn("Probe"), "probe")
    sim.step(1)
    sim.define_script("probe", [[return {
        OnStart = function() events.emit("log", "v2 start") end,
    }]])
    sim.step(1)
    assert_eq(names(sim.events("log")), { "v1 start", "v1 stop", "v2 start" })
end)

test("OnStart may wait and the other callbacks keep running", function()
    sim.define_script("waiter", [[return {
        OnStart = function(self)
            task.wait(0.05)
            events.emit("log", "woke")
        end,
        OnTick = function() events.emit("log", "tick") end,
    }]])
    sim.attach(sim.spawn("Waiter"), "waiter")
    sim.step(6)
    local log = names(sim.events("log"))
    assert_eq(log[1], "tick")
    assert_eq(table.find(log, "woke") ~= nil, true)
end)

test("an error in a late tick fails the step with the script named", function()
    sim.define_script("bad", [[return { OnLateTick = function() error("late boom") end }]])
    sim.attach(sim.spawn("Bad"), "bad")
    expect_error(function() sim.step(1) end, "late boom")
end)
