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

#include <infrastructure/math/math.hpp>
#include <rendering_engine/util/transform.hpp>

rendering_engine::util::transform::transform()
    : m_is_transform_matrix_dirty{true}, m_position{0.0f, 0.0f, 0.0f}, m_rotation{0.0f, 0.0f, 0.0f}, m_quaternion{},
      m_scale{1.0f, 1.0f, 1.0f}
{
}

void rendering_engine::util::transform::set_position(const infrastructure::math::vec3& position)
{
    m_position = position;
    m_is_transform_matrix_dirty = true;
}

void rendering_engine::util::transform::set_rotation(const infrastructure::math::vec3& rotation)
{
    m_rotation = rotation;
    m_quaternion = infrastructure::math::quat_from_euler(rotation);
    m_is_transform_matrix_dirty = true;
}

void rendering_engine::util::transform::set_quaternion(const infrastructure::math::quat& rotation)
{
    m_quaternion = infrastructure::math::normalize(rotation);
    m_rotation = infrastructure::math::euler_from_quat(m_quaternion);
    m_is_transform_matrix_dirty = true;
}

void rendering_engine::util::transform::set_scale(const infrastructure::math::vec3& scale)
{
    m_scale = scale;
    m_is_transform_matrix_dirty = true;
}

infrastructure::math::vec3 rendering_engine::util::transform::get_position() const
{
    return m_position;
}

infrastructure::math::vec3 rendering_engine::util::transform::get_rotation() const
{
    return m_rotation;
}

infrastructure::math::quat rendering_engine::util::transform::get_quaternion() const
{
    return m_quaternion;
}

infrastructure::math::vec3 rendering_engine::util::transform::get_scale() const
{
    return m_scale;
}

void rendering_engine::util::transform::look_at(const infrastructure::math::vec3& target,
                                                const infrastructure::math::vec3& up)
{
    infrastructure::math::vec3 direction = target - m_position;
    if (infrastructure::math::length(direction) <= 0.0f)
    {
        return;
    }

    m_quaternion = infrastructure::math::quat_look_at(direction, up);
    m_rotation = infrastructure::math::euler_from_quat(m_quaternion);
    m_is_transform_matrix_dirty = true;
}

infrastructure::math::vec3 rendering_engine::util::transform::get_forward() const
{
    return infrastructure::math::normalize(m_quaternion * infrastructure::math::vec3{0.0f, 0.0f, -1.0f});
}

infrastructure::math::vec3 rendering_engine::util::transform::get_right() const
{
    return infrastructure::math::normalize(m_quaternion * infrastructure::math::vec3{1.0f, 0.0f, 0.0f});
}

infrastructure::math::vec3 rendering_engine::util::transform::get_up() const
{
    return infrastructure::math::normalize(m_quaternion * infrastructure::math::vec3{0.0f, 1.0f, 0.0f});
}

infrastructure::math::mat4 rendering_engine::util::transform::get_transform_matrix() const
{
    if (!m_is_transform_matrix_dirty)
    {
        return m_transform_matrix;
    }

    using infrastructure::math::mat4;
    using infrastructure::math::scale;
    using infrastructure::math::to_mat4;
    using infrastructure::math::translate;

    mat4 pos_matrix = translate(m_position);
    mat4 rot_matrix = to_mat4(m_quaternion);
    mat4 scale_matrix = scale(m_scale);

    m_transform_matrix = pos_matrix * rot_matrix * scale_matrix;
    m_is_transform_matrix_dirty = false;
    return m_transform_matrix;
}

infrastructure::math::mat4 rendering_engine::util::transform::get_world_matrix() const
{
    return get_transform_matrix();
}
