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

#include <core/math/aabb.hpp>

namespace rendering_engine
{
    struct mesh_asset;

    namespace util
    {
        struct transform;
    }

    // World-space box of a cached @p mesh drawn under @p world: the asset's
    // object-space @c bounds transformed by the world matrix and re-boxed.
    // Shared by every renderable that draws a @ref mesh_asset (the
    // @c premade_3d primitives, @ref model) to answer
    // @ref renderable::world_bounds. Returns false and leaves @p out
    // untouched when @p mesh is null (nothing uploaded yet), so the caller
    // is treated as unbounded rather than culled.
    bool mesh_world_bounds(const mesh_asset* mesh, const util::transform& world, core::math::aabb& out);
} // namespace rendering_engine
