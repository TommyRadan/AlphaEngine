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

#include <rendering_engine/debug/box_helper.hpp>

#include <array>
#include <vector>

#include <infrastructure/math/math.hpp>
#include <rendering_engine/debug/box_edges.hpp>

namespace rendering_engine::debug
{
    box_helper::box_helper(const infrastructure::math::aabb& box, util::color color)
        : line_helper("Box"), m_box(box), m_color(color)
    {
        rebuild();
    }

    void box_helper::set_box(const infrastructure::math::aabb& box)
    {
        m_box = box;
        rebuild();
    }

    void box_helper::rebuild()
    {
        namespace math = infrastructure::math;

        // The eight corners of the box, indexed so bit 0 selects X,
        // bit 1 selects Y and bit 2 selects Z between min and max.
        const std::array<math::vec3, 8> corners{math::vec3{m_box.min.x, m_box.min.y, m_box.min.z},
                                                math::vec3{m_box.max.x, m_box.min.y, m_box.min.z},
                                                math::vec3{m_box.min.x, m_box.max.y, m_box.min.z},
                                                math::vec3{m_box.max.x, m_box.max.y, m_box.min.z},
                                                math::vec3{m_box.min.x, m_box.min.y, m_box.max.z},
                                                math::vec3{m_box.max.x, m_box.min.y, m_box.max.z},
                                                math::vec3{m_box.min.x, m_box.max.y, m_box.max.z},
                                                math::vec3{m_box.max.x, m_box.max.y, m_box.max.z}};

        std::vector<math::vec3> positions;
        std::vector<math::vec3> colors;
        build_box_edges(corners, to_rgb(m_color), positions, colors);
        set_segments(positions, colors);
    }
} // namespace rendering_engine::debug
