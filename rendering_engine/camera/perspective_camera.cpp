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
#include <rendering_engine/camera/perspective_camera.hpp>

rendering_engine::perspective_camera::perspective_camera(float field_of_view,
                                                         float aspect_ratio,
                                                         float near_clip,
                                                         float far_clip)
    : m_field_of_view{field_of_view}, m_aspect_ratio{aspect_ratio}, m_near_clip{near_clip}, m_far_clip{far_clip}
{
}

const core::math::mat4 rendering_engine::perspective_camera::get_projection_matrix() const
{
    if (!m_is_projection_matrix_dirty)
    {
        return m_projection;
    }

    m_projection = core::math::perspective(m_field_of_view, m_aspect_ratio, m_near_clip, m_far_clip);
    m_is_projection_matrix_dirty = false;
    return m_projection;
}

void rendering_engine::perspective_camera::set_field_of_view(float value)
{
    if (value == m_field_of_view)
    {
        return;
    }
    m_field_of_view = value;
    invalidate_projection_matrix();
}

float rendering_engine::perspective_camera::get_field_of_view() const noexcept
{
    return m_field_of_view;
}

void rendering_engine::perspective_camera::set_aspect_ratio(float value)
{
    if (value == m_aspect_ratio)
    {
        return;
    }
    m_aspect_ratio = value;
    invalidate_projection_matrix();
}

float rendering_engine::perspective_camera::get_aspect_ratio() const noexcept
{
    return m_aspect_ratio;
}

void rendering_engine::perspective_camera::set_near_clip(float value)
{
    if (value == m_near_clip)
    {
        return;
    }
    m_near_clip = value;
    invalidate_projection_matrix();
}

float rendering_engine::perspective_camera::get_near_clip() const noexcept
{
    return m_near_clip;
}

void rendering_engine::perspective_camera::set_far_clip(float value)
{
    if (value == m_far_clip)
    {
        return;
    }
    m_far_clip = value;
    invalidate_projection_matrix();
}

float rendering_engine::perspective_camera::get_far_clip() const noexcept
{
    return m_far_clip;
}
