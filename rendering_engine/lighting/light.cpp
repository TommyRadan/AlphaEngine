// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/lighting/light.hpp>

#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <rendering_engine/lighting/spot_light.hpp>

namespace
{
    // A forward axis shorter than this carries no direction (a zero-scale
    // node); the light keeps the direction it already has.
    constexpr float degenerate_length = 1e-6f;
} // namespace

rendering_engine::light::light(light_type type) : m_type(type) {}

rendering_engine::light_type rendering_engine::light::type() const noexcept
{
    return m_type;
}

void rendering_engine::light::set_enabled(bool enabled) noexcept
{
    m_enabled = enabled;
}

bool rendering_engine::light::is_enabled() const noexcept
{
    return m_enabled;
}

void rendering_engine::copy_light_settings(const light& settings, light_proxy& out)
{
    out.type = settings.type();
    out.color = settings.color;
    out.intensity = settings.intensity;
    switch (settings.type())
    {
    case light_type::ambient:
        out.cast_shadow = false;
        break;
    case light_type::directional:
        out.cast_shadow = static_cast<const directional_light&>(settings).cast_shadow;
        break;
    case light_type::point:
    {
        const auto& point = static_cast<const point_light&>(settings);
        out.range = point.range;
        out.constant_attenuation = point.constant_attenuation;
        out.linear_attenuation = point.linear_attenuation;
        out.quadratic_attenuation = point.quadratic_attenuation;
        out.cast_shadow = point.cast_shadow;
        break;
    }
    case light_type::spot:
    {
        const auto& spot = static_cast<const spot_light&>(settings);
        out.range = spot.range;
        out.constant_attenuation = spot.constant_attenuation;
        out.linear_attenuation = spot.linear_attenuation;
        out.quadratic_attenuation = spot.quadratic_attenuation;
        out.inner_angle = spot.inner_angle;
        out.outer_angle = spot.outer_angle;
        out.cast_shadow = spot.cast_shadow;
        break;
    }
    }
}

void rendering_engine::place_light(const core::math::mat4& world, light_proxy& out)
{
    const bool positioned = out.type == light_type::point || out.type == light_type::spot;
    const bool directed = out.type == light_type::directional || out.type == light_type::spot;
    if (positioned)
    {
        // Column 3 of the world matrix is the world translation.
        out.position = core::math::vec3{world.m[12], world.m[13], world.m[14]};
    }
    if (directed)
    {
        // Column 0 is the world +X axis, the forward of the engine
        // convention; its length is the x scale.
        const core::math::vec3 forward{world.m[0], world.m[1], world.m[2]};
        const float forward_length = core::math::length(forward);
        if (forward_length > degenerate_length)
        {
            out.direction = forward / forward_length;
        }
    }
}
