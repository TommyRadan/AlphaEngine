// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/math.hpp>
#include <rendering_engine/camera/camera.hpp>

namespace rendering_engine
{
    /**
     * @brief Orthographic projection over [-x, x] by [-y, y]. The
     *        magnifications and clip planes are private so every change
     *        goes through a setter that invalidates the cached projection.
     *        The drawable aspect is ignored (see @ref camera::set_aspect_ratio).
     */
    struct orthographic_camera : public camera
    {
        orthographic_camera();

        const core::math::mat4 get_projection_matrix() const final;

        void set_x_magnification(float value);
        float get_x_magnification() const noexcept;

        void set_y_magnification(float value);
        float get_y_magnification() const noexcept;

        void set_near_clip(float value);
        float get_near_clip() const noexcept;

        void set_far_clip(float value);
        float get_far_clip() const noexcept;

    private:
        float m_x_magnification;
        float m_y_magnification;
        float m_near_clip;
        float m_far_clip;
    };
} // namespace rendering_engine
