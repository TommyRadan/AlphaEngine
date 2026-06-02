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

#pragma once

#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/debug/helper.hpp>
#include <rendering_engine/renderables/line.hpp>
#include <rendering_engine/util/transform.hpp>

namespace rendering_engine::debug
{
    // A helper whose geometry is a list of independent line segments
    // (vertex pairs) drawn through the shared depth-disabled debug line
    // material, in the always-on-top overlay pass. The line-based gizmos
    // (axes, bounding box, light and camera wireframes) derive from this;
    // they fill geometry via @ref set_segments and optionally follow a
    // moving target via @ref refresh.
    struct line_helper : public helper
    {
        explicit line_helper(const char* name);
        ~line_helper() override;

        // World placement of the gizmo. Helpers that bake their geometry
        // around the origin (axes) use it; helpers that bake world-space
        // geometry directly (box, light, camera) leave it at identity.
        util::transform transform;

        void upload() final;
        void collect_draw_items(std::vector<draw_item>& out) final;

    protected:
        // Replace the gizmo geometry with @p positions interpreted as
        // independent line segments (vertex pairs) and upload it. The two
        // vectors must be the same length.
        void set_segments(const std::vector<core::math::vec3>& positions, const std::vector<core::math::vec3>& colors);

        // Rebuild geometry from the helper's target before drawing.
        // Static helpers (axes) build once in their constructor and leave
        // this a no-op; helpers that follow a moving target (light,
        // camera) override it. Invoked from @ref collect_draw_items.
        virtual void refresh();

    private:
        line m_line;
    };
} // namespace rendering_engine::debug
