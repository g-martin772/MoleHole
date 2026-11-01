local pid = require("lib/pid")
local plain = require("lib/plain")

test("proportional term and output clamp", function()
    local p = pid.new({ kp = 2, out_min = -3, out_max = 3 })
    assert_eq(pid.step(p, 1, 0, 0.1), 2)
    assert_eq(pid.step(p, 10, 0, 0.1), 3)
    assert_eq(pid.step(p, -10, 0, 0.1), -3)
    assert_eq(pid.step(p, 4, 4, 0.1), 0)
end)

test("integral accumulates ki * error * dt", function()
    local p = pid.new({ ki = 3 })
    local out
    for i = 1, 10 do out = pid.step(p, 2, 0, 0.1) end
    assert_near(out, 3 * 2 * 1, 1e-12)
    pid.reset(p)
    assert_eq(pid.step(p, 0, 0, 0.1), 0)
end)

test("i_limit caps the integral term", function()
    local p = pid.new({ ki = 10, i_limit = 0.5 })
    for i = 1, 100 do pid.step(p, 5, 0, 0.1) end
    assert_eq(pid.step(p, 5, 0, 0.1), 0.5)
    assert_eq(pid.step(p, -5, 0, 0.1) < 0.5, true)
end)

test("clamping anti-windup stops the integral while saturated", function()
    local p = pid.new({ kp = 1, ki = 5, out_min = -1, out_max = 1 })
    for i = 1, 600 do pid.step(p, 100, 0, 1 / 60) end
    assert(p.integral[1] <= 1 + 1e-9, "integral " .. p.integral[1])
    local recovered
    for i = 1, 30 do recovered = pid.step(p, -100, 0, 1 / 60) end
    assert_eq(recovered, -1)
end)

test("integral keeps working while the output is saturated the other way", function()
    local p = pid.new({ kp = 0, ki = 1, out_min = -1, out_max = 1 })
    for i = 1, 100 do pid.step(p, 100, 0, 0.1) end
    local before = p.integral[1]
    pid.step(p, -100, 0, 0.1)
    assert(p.integral[1] < before, "unwinds")
end)

test("derivative acts on the measurement, not the setpoint", function()
    local p = pid.new({ kp = 0, kd = 2 })
    assert_eq(pid.step(p, 0, 5, 0.1), 0)
    assert_eq(pid.step(p, 100, 5, 0.1), 0)
    assert_near(pid.step(p, 100, 5.5, 0.1), -2 * 5, 1e-9)
    assert_near(pid.step(p, 100, 5.5, 0.1), 0, 1e-9)
end)

test("a supplied rate replaces the differenced derivative", function()
    local p = pid.new({ kp = 1, kd = 0.5 })
    assert_near(pid.step(p, 1, 0, 0.1, 4), 1 - 2, 1e-12)
    assert_near(pid.step(p, 1, 123, 0.1, -2), -122 + 1, 1e-12)
end)

test("derivative filter smooths a measurement step", function()
    local raw = pid.new({ kd = 1 })
    local filtered = pid.new({ kd = 1, d_tau = 0.2 })
    pid.step(raw, 0, 0, 0.01)
    pid.step(filtered, 0, 0, 0.01)
    local r = pid.step(raw, 0, 1, 0.01)
    local f = pid.step(filtered, 0, 1, 0.01)
    assert_near(r, -100, 1e-9)
    assert_near(f, -100 * (0.01 / 0.21), 1e-9)
end)

test("a non-positive dt returns the last output and changes nothing", function()
    local p = pid.new({ kp = 1, ki = 1 })
    local out = pid.step(p, 3, 0, 0.1)
    local snapshot = plain.copy(p)
    assert_eq(pid.step(p, 99, 0, 0), out)
    assert_eq(pid.step(p, 99, 0, -1), out)
    assert_eq(p, snapshot)
end)

local function run_plant(gains, disturbance, steps)
    local p = pid.new(gains)
    local x, v = 0, 0
    for i = 1, steps do
        local u = pid.step(p, 5, x, 1 / 100)
        v += (u + disturbance) * (1 / 100)
        x += v * (1 / 100)
    end
    return x, v
end

test("PD drives a double integrator to the setpoint", function()
    local x, v = run_plant({ kp = 20, kd = 9 }, 0, 1500)
    assert_near(x, 5, 1e-3)
    assert_near(v, 0, 1e-3)
end)

test("the integral term removes a constant disturbance offset", function()
    local biased = run_plant({ kp = 20, kd = 9 }, -4, 3000)
    assert_near(biased, 5 - 4 / 20, 1e-2)
    local fixed = run_plant({ kp = 20, kd = 9, ki = 30 }, -4, 6000)
    assert_near(fixed, 5, 1e-2)
end)

test("vector channels are independent and keep the input type", function()
    local p = pid.new_vec({ kp = 2, ki = 1, out_min = -1.5, out_max = 1.5 })
    local out = pid.step_vec(p, vector.create(1, -1, 0), vector.create(0, 0, 0), 0.1)
    assert_eq(typeof(out), "vector")
    assert_near(out, vector.create(1.5, -1.5, 0), 1e-12)
    for i = 1, 5 do out = pid.step_vec(p, vector.create(0.1, 0, 0), vector.create(0, 0, 0), 0.1) end
    assert_near(out.x, 0.25, 1e-6)
    local wide = pid.new_vec({ kp = 1 })
    local far = pid.step_vec(wide, dvec3.new(1e11 + 0.5, 0, 0), dvec3.new(1e11, 2, 0), 0.1)
    assert_eq(typeof(far), "dvec3")
    assert_eq(far.x, 0.5)
    assert_eq(far.y, -2)
    assert_eq(pid.step_vec(wide, dvec3.new(1, 1, 1), dvec3.new(0, 0, 0), 0).x, far.x)
end)

test("vector derivative takes a per-axis rate", function()
    local p = pid.new_vec({ kd = 1 })
    local out = pid.step_vec(p, vector.create(0, 0, 0), vector.create(0, 0, 0), 0.1, vector.create(1, 2, 3))
    assert_near(out, vector.create(-1, -2, -3), 1e-12)
end)

test("state is plain data and a copy continues identically", function()
    local a = pid.new_vec({ kp = 3, ki = 2, kd = 0.5, out_min = -2, out_max = 2, d_tau = 0.05 })
    assert(plain.check(a))
    local x = vector.create(0, 0, 0)
    for i = 1, 40 do
        x += pid.step_vec(a, vector.create(1, -2, 3), x, 1 / 60) * (1 / 60)
    end
    local ok, where = plain.check(a)
    assert(ok, where)
    local b = plain.copy(a)
    local xa, xb = x, x
    for i = 1, 60 do
        xa += pid.step_vec(a, vector.create(1, -2, 3), xa, 1 / 60) * (1 / 60)
        xb += pid.step_vec(b, vector.create(1, -2, 3), xb, 1 / 60) * (1 / 60)
    end
    assert_eq(xa, xb)
    assert_eq(a, b)
end)

local SCRIPT = [[
local pid = require("lib/pid")
local plain = require("lib/plain")
return {
    OnStart = function(self)
        self.pid = pid.new({ kp = 12, ki = 6, kd = 8, out_min = -50, out_max = 50 })
        self.x, self.v = 0, 0
    end,
    OnTick = function(self, dt)
        self.n = (self.n or 0) + 1
        if self.n == 100 and scene.name(self.entity) == "B" then
            self.pid = plain.copy(self.pid)
        end
        local u = pid.step(self.pid, 3, self.x, dt)
        self.v += (u - 0.5 * self.v) * dt
        self.x += self.v * dt
        scene.set(self.entity, "Transform", "Position", vector.create(self.x, 0, 0))
    end,
}
]]

test("a PID script gives the same result every run, with or without a state round trip", function()
    sim.define_script("pid_probe", SCRIPT)
    sim.set_dt(1 / 64)
    local a, b = sim.spawn("A"), sim.spawn("B")
    sim.attach(a, "pid_probe")
    sim.attach(b, "pid_probe")
    sim.step(900)
    assert_eq(scene.get(a, "Transform", "Position"), scene.get(b, "Transform", "Position"))
    assert_near(scene.get(a, "Transform", "Position").x, 3, 0.01)
end)
