// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/math/math.hpp>
#include <rendering_engine/camera/camera.hpp>

namespace
{
    // A basis column shorter than this carries no direction (a zero-scale
    // transform); the view falls back to the identity orientation.
    constexpr float degenerate_length = 1e-6f;
} // namespace

rendering_engine::camera::camera() : m_is_projection_matrix_dirty{true} {}

core::math::mat4 rendering_engine::view_matrix_from_world(const core::math::mat4& world)
{
    // Column-major: columns 0 / 2 are the world-space images of the local
    // +X (forward) and +Z (up) axes, column 3 the translation. Each axis is
    // normalised so a scaled transform (say a camera under a scaled node)
    // does not scale the view. core::math::look_at builds the view-space
    // frame (-Z forward, +Y up) from this world-space frame.
    const core::math::vec3 position{world.m[12], world.m[13], world.m[14]};
    core::math::vec3 forward{world.m[0], world.m[1], world.m[2]};
    core::math::vec3 up{world.m[8], world.m[9], world.m[10]};

    const float forward_length = core::math::length(forward);
    const float up_length = core::math::length(up);
    if (forward_length <= degenerate_length || up_length <= degenerate_length)
    {
        forward = core::math::world_forward;
        up = core::math::world_up;
    }
    else
    {
        forward /= forward_length;
        up /= up_length;
    }

    return core::math::look_at(position, position + forward, core::math::reference_up(forward, up));
}

void rendering_engine::camera::invalidate_projection_matrix()
{
    m_is_projection_matrix_dirty = true;
}

void rendering_engine::camera::set_enabled(bool enabled) noexcept
{
    m_enabled = enabled;
}

bool rendering_engine::camera::is_enabled() const noexcept
{
    return m_enabled;
}

void rendering_engine::camera::set_priority(int priority) noexcept
{
    m_priority = priority;
}

int rendering_engine::camera::get_priority() const noexcept
{
    return m_priority;
}

void rendering_engine::camera::set_main(bool main) noexcept
{
    m_main = main;
}

bool rendering_engine::camera::is_main() const noexcept
{
    return m_main;
}

void rendering_engine::camera::set_culling_mask(uint32_t mask) noexcept
{
    m_culling_mask = mask;
}

uint32_t rendering_engine::camera::culling_mask() const noexcept
{
    return m_culling_mask;
}

void rendering_engine::camera::set_target(const render_texture* target) noexcept
{
    m_target = target;
}

const rendering_engine::render_texture* rendering_engine::camera::target() const noexcept
{
    return m_target;
}

void rendering_engine::camera::set_viewport(const viewport_rect& viewport) noexcept
{
    m_viewport = viewport;
}

const rendering_engine::viewport_rect& rendering_engine::camera::viewport() const noexcept
{
    return m_viewport;
}

void rendering_engine::camera::set_draws_ui(bool draws_ui) noexcept
{
    m_draws_ui = draws_ui;
}

bool rendering_engine::camera::draws_ui() const noexcept
{
    return m_draws_ui;
}
