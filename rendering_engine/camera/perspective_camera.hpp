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

#include <cstdint>

#include <core/math/math.hpp>
#include <rendering_engine/camera/camera.hpp>

namespace rendering_engine
{
    /**
     * @brief Width over height of a drawable, or @p fallback when either
     *        dimension is zero (no window yet, or a minimised one).
     *
     * Pure helper the renderer uses to turn the drawable's pixel size into
     * the aspect it reports to the camera registry at init and on resize.
     */
    constexpr float drawable_aspect_ratio(std::uint32_t width, std::uint32_t height, float fallback) noexcept
    {
        if (width == 0 || height == 0)
        {
            return fallback;
        }
        return static_cast<float>(width) / static_cast<float>(height);
    }

    /**
     * @brief Perspective projection. The parameters are private so every
     *        change goes through a setter that invalidates the cached
     *        projection; nothing about the projection can go stale.
     *
     * The camera itself reads no engine state: the creator supplies the
     * vertical field of view (radians) and the aspect, and the camera
     * registry keeps the aspect of an attached camera in step with the
     * drawable through @ref set_aspect_ratio.
     */
    struct perspective_camera : public camera
    {
        perspective_camera(float field_of_view, float aspect_ratio, float near_clip = 0.1f, float far_clip = 10000.0f);

        const core::math::mat4 get_projection_matrix() const final;

        void set_field_of_view(float value);
        float get_field_of_view() const noexcept;

        void set_aspect_ratio(float value) final;
        float get_aspect_ratio() const noexcept;

        void set_near_clip(float value);
        float get_near_clip() const noexcept;

        void set_far_clip(float value);
        float get_far_clip() const noexcept;

    private:
        float m_field_of_view;
        float m_aspect_ratio;
        float m_near_clip;
        float m_far_clip;
    };
} // namespace rendering_engine
