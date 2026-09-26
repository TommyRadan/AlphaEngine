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
#include <rendering_engine/camera/orthographic_camera.hpp>

rendering_engine::orthographic_camera::orthographic_camera()
    : m_x_magnification{1.0f}, m_y_magnification{1.0f}, m_near_clip{0.1f}, m_far_clip{10000.0f}
{
}

const core::math::mat4 rendering_engine::orthographic_camera::get_projection_matrix() const
{
    if (!m_is_projection_matrix_dirty)
    {
        return m_projection;
    }

    m_projection = core::math::ortho(
        -m_x_magnification, m_x_magnification, -m_y_magnification, m_y_magnification, m_near_clip, m_far_clip);
    m_is_projection_matrix_dirty = false;
    return m_projection;
}

void rendering_engine::orthographic_camera::set_x_magnification(float value)
{
    if (value == m_x_magnification)
    {
        return;
    }
    m_x_magnification = value;
    invalidate_projection_matrix();
}

float rendering_engine::orthographic_camera::get_x_magnification() const noexcept
{
    return m_x_magnification;
}

void rendering_engine::orthographic_camera::set_y_magnification(float value)
{
    if (value == m_y_magnification)
    {
        return;
    }
    m_y_magnification = value;
    invalidate_projection_matrix();
}

float rendering_engine::orthographic_camera::get_y_magnification() const noexcept
{
    return m_y_magnification;
}

void rendering_engine::orthographic_camera::set_near_clip(float value)
{
    if (value == m_near_clip)
    {
        return;
    }
    m_near_clip = value;
    invalidate_projection_matrix();
}

float rendering_engine::orthographic_camera::get_near_clip() const noexcept
{
    return m_near_clip;
}

void rendering_engine::orthographic_camera::set_far_clip(float value)
{
    if (value == m_far_clip)
    {
        return;
    }
    m_far_clip = value;
    invalidate_projection_matrix();
}

float rendering_engine::orthographic_camera::get_far_clip() const noexcept
{
    return m_far_clip;
}
