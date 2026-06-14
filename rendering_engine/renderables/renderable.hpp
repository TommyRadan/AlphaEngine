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

#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    // Anything that can contribute draws to a render pass. @ref upload
    // allocates GPU resources (buffers, bind groups) once GL is alive;
    // @ref collect_draw_items appends one or more @ref draw_item values
    // describing what to draw this frame. The pass sorts the collected
    // items by material and dispatches them in one place — renderables
    // never call @c set_pipeline / @c set_bind_group / @c draw_indexed
    // themselves.
    //
    // The output vector is provided by the caller so the pass can reuse
    // a single allocation across frames; composite renderables (e.g.
    // @c label) push N items into the same vector.
    struct renderable
    {
        virtual ~renderable() = default;
        virtual void upload() = 0;
        virtual void collect_draw_items(std::vector<draw_item>& out) = 0;

        // Whether this renderable contributes occluders to the shadow
        // passes. Defaults to true; non-physical geometry (debug
        // helpers, fullscreen-triangle effects) overrides it to false so
        // the depth-only shadow pipeline never rasterizes its clip-space
        // or gizmo vertices into the shadow map as a phantom caster.
        virtual bool casts_shadow() const
        {
            return true;
        }
    };
} // namespace rendering_engine
