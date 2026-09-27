-- Rises and sinks while slowly turning about the up axis. Attached to a
-- pillar of the shadow demo (external/shadow_demo_module.cpp); edit and
-- save it while a Debug build runs to see it reload.
local bob = {
    properties = {
        height = 0.75, -- how far above its resting place the node rises
        period = 3.0, -- seconds per rise and fall
        spin = 0.5, -- radians per second about the up axis
    },
}

function bob:on_start()
    self.base = self.node.position
    self.time = 0
end

function bob:on_update(dt)
    self.time = self.time + dt / 1000
    local lift = 0.5 - 0.5 * math.cos(self.time * 2 * math.pi / self.period)
    self.node.position = self.base + vec3(0, 0, self.height * lift)
    self.node.rotation = quat.from_euler(vec3(0, 0, self.time * self.spin))
end

return bob
