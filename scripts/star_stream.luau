return {
    properties = {
        { name = "Hole", type = "entity" },
        { name = "MassRate", type = "float", unit = "kg/s", default = 6.3e14, min = 0, max = 1e30 },
        { name = "ParticlesPerSecond", type = "float", default = 2000, min = 1, max = 1e9 },
        { name = "Speed", type = "float", unit = "m/s", default = 5e6, min = 0, max = 3e8 },
        { name = "Dispersion", type = "float", unit = "m/s", default = 1e5, min = 0, max = 3e8 },
        { name = "StreamRadius", type = "float", unit = "m", default = 1e4, min = 0, max = 1e15 },
        { name = "Temperature", type = "float", unit = "K", default = 1e4, min = 0, max = 1e12 },
        { name = "Seed", type = "int", default = 7 },
    },

    OnStart = function(self: ScriptSelf)
        local props = self.props
        local fx, fy, fz = scene.get_xyz(self.entity, "Transform", "Position")
        local tx, ty, tz = scene.get_xyz(props.Hole, "Transform", "Position")
        self.emitter = particles.emitter{
            shape = "stream",
            from = vector.create(fx, fy, fz),
            toward = vector.create(tx, ty, tz),
            mass_rate = props.MassRate,
            rate = props.ParticlesPerSecond,
            speed = props.Speed,
            dispersion = props.Dispersion,
            radius = props.StreamRadius,
            temperature = props.Temperature,
            seed = props.Seed,
        }
    end,

    OnTick = function(self: ScriptSelf, dt: number)
        local props = self.props
        if self.emitter == nil or props.Hole == nil or not scene.exists(props.Hole) then
            return
        end
        particles.set_origin(self.emitter, scene.get_xyz(self.entity, "Transform", "Position"))
        particles.set_rate(self.emitter, props.MassRate)
    end,
}
