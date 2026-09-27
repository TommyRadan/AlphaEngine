// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

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
