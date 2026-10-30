local orbit = {}

function orbit.direction(yawDegrees: number, pitchDegrees: number): (number, number, number)
    local yaw, pitch = math.rad(yawDegrees), math.rad(pitchDegrees)
    return math.cos(pitch) * math.sin(yaw), math.sin(pitch), math.cos(pitch) * math.cos(yaw)
end

return orbit
