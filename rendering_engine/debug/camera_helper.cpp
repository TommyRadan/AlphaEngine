/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
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

#include <rendering_engine/debug/camera_helper.hpp>

#include <array>
#include <vector>

#include <infrastructure/math/math.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/debug/box_edges.hpp>

namespace rendering_engine::debug
{
    camera_helper::camera_helper(const camera* cam, util::color color)
        : line_helper("Camera"), m_camera(cam), m_color(color)
    {
    }

    void camera_helper::refresh()
    {
        if (m_camera == nullptr)
        {
            return;
        }

        namespace math = infrastructure::math;

        const math::mat4 view_projection = m_camera->get_projection_matrix() * m_camera->get_view_matrix();
        if (m_built && view_projection == m_last_view_projection)
        {
            return;
        }
        m_last_view_projection = view_projection;
        m_built = true;

        // Unproject the eight clip-cube corners back into world space.
        // GLM builds projections with z in [-1, 1] (the camera's frustum
        // extraction uses the same matrix), so the near plane is z = -1
        // and the far plane z = 1. Corners are indexed bit 0 = X,
        // bit 1 = Y, bit 2 = Z to match build_box_edges().
        const math::mat4 inverse_vp = math::inverse(view_projection);
        std::array<math::vec3, 8> corners{};
        for (int i = 0; i < 8; ++i)
        {
            const float x = (i & 1) != 0 ? 1.0f : -1.0f;
            const float y = (i & 2) != 0 ? 1.0f : -1.0f;
            const float z = (i & 4) != 0 ? 1.0f : -1.0f;
            const math::vec4 clip = inverse_vp * math::vec4{x, y, z, 1.0f};
            const float inv_w = clip.w != 0.0f ? 1.0f / clip.w : 1.0f;
            corners[static_cast<size_t>(i)] = math::vec3{clip.x * inv_w, clip.y * inv_w, clip.z * inv_w};
        }

        std::vector<math::vec3> positions;
        std::vector<math::vec3> colors;
        build_box_edges(corners, to_rgb(m_color), positions, colors);
        set_segments(positions, colors);
    }
} // namespace rendering_engine::debug
