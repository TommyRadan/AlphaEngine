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

#include <rendering_engine/debug/line_helper.hpp>

#include <control/engine.hpp>
#include <rendering_engine/materials/line_material.hpp>
#include <rendering_engine/rendering_engine.hpp>

namespace rendering_engine::debug
{
    line_helper::line_helper(const char* name)
        : helper(name, helper_layer::overlay), m_line(&control::current_engine().renderer->get_debug_line_material())
    {
        // Every gizmo is a list of independent segments (vertex pairs).
        m_line.set_mode(line_mode::segments);
    }

    line_helper::~line_helper() = default;

    void line_helper::upload()
    {
        // Geometry is uploaded eagerly in set_segments(); nothing to do
        // when the pass requests an upload.
    }

    void line_helper::set_segments(const std::vector<infrastructure::math::vec3>& positions,
                                   const std::vector<infrastructure::math::vec3>& colors)
    {
        m_line.set_positions(positions, colors);
        m_line.upload();
    }

    void line_helper::refresh() {}

    void line_helper::collect_draw_items(std::vector<draw_item>& out)
    {
        if (!visible)
        {
            return;
        }

        // Let dynamic gizmos follow their target before they draw.
        refresh();

        // Place the (mostly origin-baked) geometry; helpers that bake
        // world-space vertices leave the transform at identity.
        m_line.transform = transform;
        m_line.collect_draw_items(out);
    }
} // namespace rendering_engine::debug
