local zoom = {}

function zoom.step(self, props, dt: number, invert: boolean?): number
    local lo, hi = math.min(props.ZoomMin, props.ZoomMax), math.max(props.ZoomMin, props.ZoomMax)
    local goal = self.zoom_goal or 1
    local scroll = input.scroll()
    if scroll ~= 0 then
        goal = goal * math.exp((invert and scroll or -scroll) * props.ZoomSpeed)
    end
    goal = math.clamp(goal, lo, hi)
    self.zoom_goal = goal
    local value = self.zoom or 1
    local k = props.ZoomSmooth > 0 and 1 - math.exp(-dt / props.ZoomSmooth) or 1
    value = value * (goal / value) ^ k
    if math.abs(value / goal - 1) < 1e-6 then
        value = goal
    end
    self.zoom = value
    return value
end

return zoom
