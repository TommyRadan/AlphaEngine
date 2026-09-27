// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

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
