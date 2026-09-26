/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <core/math/math.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/camera/camera_registry.hpp>

namespace
{
    // A basis column shorter than this carries no direction (a zero-scale
    // transform); the view falls back to the identity orientation.
    constexpr float degenerate_length = 1e-6f;
} // namespace

rendering_engine::camera::camera() : m_is_projection_matrix_dirty{true} {}

rendering_engine::camera::~camera()
{
    // A no-op for a camera that was never attached, or already detached.
    detach();
}

void rendering_engine::camera::look_at(const core::math::vec3& target, const core::math::vec3& up)
{
    const util::transform* parent = transform.get_parent();
    if (parent == nullptr)
    {
        transform.look_at(target, up);
        return;
    }

    // The transform's look_at works in its parent's frame: re-express the
    // world-space target (a point) and up (a direction) there first.
    const core::math::mat4 parent_inverse = core::math::inverse(parent->get_world_matrix());
    const core::math::vec4 local_target = parent_inverse * core::math::vec4{target, 1.0f};
    const core::math::vec4 local_up = parent_inverse * core::math::vec4{up, 0.0f};
    transform.look_at(core::math::vec3{local_target.x, local_target.y, local_target.z},
                      core::math::vec3{local_up.x, local_up.y, local_up.z});
}

core::math::mat4 rendering_engine::camera::get_view_matrix() const
{
    // Column-major: columns 0 / 2 are the world-space images of the local
    // +X (forward) and +Z (up) axes, column 3 the translation. Each axis is
    // normalised so a scaled transform (say a camera under a scaled node)
    // does not scale the view. core::math::look_at builds the GL-style view
    // frame (-Z forward, +Y up) from this world-space frame.
    const core::math::mat4 world = transform.get_world_matrix();
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

const core::math::frustum rendering_engine::camera::get_frustum() const
{
    return core::math::frustum::from_view_projection(get_projection_matrix() * get_view_matrix());
}

void rendering_engine::camera::attach()
{
    register_camera(*this);
}

void rendering_engine::camera::detach()
{
    unregister_camera(*this);
}

bool rendering_engine::camera::is_attached() const
{
    return is_registered(*this);
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
