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

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/materials/material.hpp>

namespace rendering_engine
{
    // Built-in 3D scene material. Position-only vertex stream, MVP
    // transform in the vertex shader, flat-white fragment. Per-draw
    // bind group at slot 1 carries @c modelMatrix; the per-frame group
    // (camera @c viewMatrix / @c projectionMatrix) at slot 0 is owned
    // and bound by the @ref scene_pass.
    struct lit_material : public material
    {
        // @p frame_layout is the per-frame bind-group layout owned by
        // the @ref scene_pass. It must match the layout the pass binds
        // at slot 0 every frame, so the pipeline and the runtime bind
        // group agree on slot shape.
        explicit lit_material(gpu::bind_group_layout frame_layout);
        ~lit_material() override = default;
    };
} // namespace rendering_engine
