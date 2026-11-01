local plain = require("lib/plain")

test("check accepts numbers, strings, booleans, vectors and nested tables", function()
    local ok = plain.check({ a = 1, b = "x", c = true, d = vector.create(1, 2, 3), e = { 1, 2, { f = math.huge } }, [5] = 0 })
    assert_eq(ok, true)
end)

test("check names the first offender", function()
    local ok, where = plain.check({ fn = function() end })
    assert_eq(ok, false)
    assert_eq(where, "state.fn is a function")
    ok, where = plain.check({ list = { dvec3.new(1, 2, 3) } }, "self.thing")
    assert_eq(ok, false)
    assert_eq(where, "self.thing.list.1 is a dvec3")
    ok, where = plain.check(setmetatable({}, {}))
    assert_eq(ok, false)
    assert_eq(where, "state has a metatable")
    local loop = {}
    loop.me = loop
    ok, where = plain.check(loop)
    assert_eq(ok, false)
    assert_eq(where, "state.me is a cycle")
    ok, where = plain.check({ [true] = 1 })
    assert_eq(ok, false)
    assert_eq(where, "state has a boolean key")
end)

test("check enforces the save depth limit", function()
    local value = { leaf = 1 }
    for i = 1, 10 do value = { value } end
    assert_eq(plain.check(value), true)
    local _, where = plain.check({ value })
    assert_eq(where ~= nil, true)
end)

test("copy is deep and independent", function()
    local source = { list = { 1, 2, 3 }, v = vector.create(1, 2, 3), name = "n" }
    local copy = plain.copy(source)
    assert_eq(copy, source)
    copy.list[1] = 99
    assert_eq(source.list[1], 1)
    assert_eq(plain.copy(5), 5)
end)
