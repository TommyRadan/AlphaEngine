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
     * Pure helper shared by the @ref perspective_camera constructor (which
     * reads the window's pixel size) and the renderer's resize path.
     */
    constexpr float drawable_aspect_ratio(std::uint32_t width, std::uint32_t height, float fallback) noexcept
    {
        if (width == 0 || height == 0)
        {
            return fallback;
        }
        return static_cast<float>(width) / static_cast<float>(height);
    }

    struct perspective_camera : public camera
    {
        // Reads the field of view from the settings and the aspect ratio
        // from the window's current pixel size, falling back to the
        // settings' nominal size while the window has no drawable. The
        // renderer keeps the attached camera's aspect current across
        // resizes through @ref set_aspect_ratio.
        perspective_camera();

        const core::math::mat4 get_projection_matrix() const final;

        // Sets @ref aspect_ratio and invalidates the projection.
        void set_aspect_ratio(float value) final;

        float field_of_view;
        float aspect_ratio;
        float near_clip;
        float far_clip;
    };
} // namespace rendering_engine
