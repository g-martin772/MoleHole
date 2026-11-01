local orbit = require("lib/orbit")

test("zero yaw and pitch faces +z", function()
    local x, y, z = orbit.direction(0, 0)
    assert_near(x, 0)
    assert_near(y, 0)
    assert_near(z, 1)
end)

test("yaw turns toward +x and pitch lifts toward +y", function()
    local x, y, z = orbit.direction(90, 0)
    assert_near(vector.create(x, y, z), vector.create(1, 0, 0))
    x, y, z = orbit.direction(0, 90)
    assert_near(vector.create(x, y, z), vector.create(0, 1, 0))
    x, y, z = orbit.direction(180, -30)
    assert_near(vector.create(x, y, z), vector.create(0, -0.5, -math.sqrt(3) / 2))
end)

test("the direction is always a unit vector", function()
    for yaw = -360, 360, 37 do
        for pitch = -90, 90, 15 do
            local x, y, z = orbit.direction(yaw, pitch)
            assert_near(math.sqrt(x * x + y * y + z * z), 1, 1e-12, "yaw " .. yaw .. " pitch " .. pitch)
        end
    end
end)
