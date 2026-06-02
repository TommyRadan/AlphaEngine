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
#include <core/settings.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/rendering_engine.hpp>

rendering_engine::camera* rendering_engine::camera::m_current_camera = nullptr;

rendering_engine::camera::camera()
{
    m_is_view_matrix_dirty = true;
    m_is_projection_matrix_dirty = true;

    transform.set_rotation({1.0f, 0.0f, 0.0f});
}

void rendering_engine::camera::attach()
{
    rendering_engine::camera::m_current_camera = this;
}

void rendering_engine::camera::detach()
{
    rendering_engine::camera::m_current_camera = nullptr;
}

rendering_engine::camera* rendering_engine::camera::get_current_camera()
{
    return rendering_engine::camera::m_current_camera;
}

void rendering_engine::camera::look_at(const core::math::vec3& target)
{
    core::math::vec3 direction = target - transform.get_position();
    if (core::math::length(direction) <= 0.0f)
    {
        return;
    }

    // The view matrix derives its look-at target from the position plus the rotation vector, so the
    // camera's orientation lives in that rotation as a forward direction (see get_view_matrix).
    transform.set_rotation(core::math::normalize(direction));
    invalidate_view_matrix();
}

void rendering_engine::camera::invalidate_view_matrix()
{
    m_is_view_matrix_dirty = true;
}

const core::math::mat4 rendering_engine::camera::get_view_matrix() const
{
    if (!m_is_view_matrix_dirty)
    {
        return m_view_matrix;
    }

    core::math::vec3 up_vector{0.0f, 0.0f, 1.0f};
    core::math::vec3 look_at_target = transform.get_position() + transform.get_rotation();
    m_view_matrix = core::math::look_at(transform.get_position(), look_at_target, up_vector);
    m_is_view_matrix_dirty = false;
    return m_view_matrix;
}

void rendering_engine::camera::invalidate_projection_matrix()
{
    m_is_projection_matrix_dirty = true;
}

const core::math::frustum rendering_engine::camera::get_frustum() const
{
    return core::math::frustum::from_view_projection(get_projection_matrix() * get_view_matrix());
}
