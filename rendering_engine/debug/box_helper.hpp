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

#include <infrastructure/math/aabb.hpp>
#include <rendering_engine/debug/helper.hpp>
#include <rendering_engine/util/color.hpp>

namespace rendering_engine::debug
{
    // Wireframe of an axis-aligned bounding box — the analogue of
    // @c THREE.Box3Helper. The twelve edges are baked in world space, so
    // the inherited @ref transform is left at identity; call
    // @ref set_box to follow a box that moves.
    struct box_helper : public helper
    {
        explicit box_helper(const infrastructure::math::aabb& box = infrastructure::math::aabb{},
                            util::color color = util::color{255, 255, 0, 255});

        // Replace the box and rebuild the wireframe.
        void set_box(const infrastructure::math::aabb& box);

    private:
        void rebuild();

        infrastructure::math::aabb m_box;
        util::color m_color;
    };
} // namespace rendering_engine::debug
