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
