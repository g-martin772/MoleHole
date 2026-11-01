local plain = {}

local MAX_DEPTH = 12

local function check(value, path, depth, seen)
    local kind = type(value)
    if kind == "number" or kind == "boolean" or kind == "string" or kind == "vector" then
        return true, nil
    elseif kind ~= "table" then
        return false, path .. " is a " .. typeof(value)
    elseif getmetatable(value) ~= nil then
        return false, path .. " has a metatable"
    elseif depth >= MAX_DEPTH then
        return false, path .. " is nested too deep"
    elseif seen[value] then
        return false, path .. " is a cycle"
    end
    seen[value] = true
    for k, v in value do
        local key_kind = type(k)
        if key_kind ~= "string" and key_kind ~= "number" then
            return false, path .. " has a " .. key_kind .. " key"
        end
        local ok, where = check(v, path .. "." .. tostring(k), depth + 1, seen)
        if not ok then
            return false, where
        end
    end
    seen[value] = nil
    return true, nil
end

function plain.check(value: any, name: string?): (boolean, string?)
    return check(value, name or "state", 1, {})
end

function plain.copy(value: any): any
    if type(value) ~= "table" then
        return value
    end
    local out = {}
    for k, v in value do
        out[k] = plain.copy(v)
    end
    return out
end

return plain
