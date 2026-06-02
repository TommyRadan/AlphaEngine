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

#include <cstdint>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine
{
    struct material;

    // One draw the pass can dispatch. Renderables fill this struct in
    // @ref renderable::collect_draw_items; the pass then sorts the
    // collected items by @c mat->pipeline().id and walks them, issuing
    // @c set_pipeline only when the pipeline changes. An invalid
    // @ref index_buffer means non-indexed draw — the pass calls
    // @c draw(vertex_count) instead of @c draw_indexed(index_count).
    //
    // A valid @ref indirect_buffer turns the indexed draw into an
    // indexed *indirect* draw: the pass binds the index buffer and
    // calls @c draw_indexed_indirect, sourcing the index count and the
    // instance count from the buffer's @c DrawElementsIndirectCommand
    // record instead of from @ref index_count. This is how
    // @ref instanced_mesh emits a single instanced draw over shared
    // geometry; only the index path supports it.
    struct draw_item
    {
        material* mat{nullptr};
        gpu::buffer vertex_buffer{};
        gpu::buffer index_buffer{};
        gpu::buffer indirect_buffer{};
        gpu::bind_group per_draw_bind_group{};
        uint32_t vertex_count{0};
        uint32_t index_count{0};
        uint32_t vertex_stride{0};
        gpu::index_format index_format{gpu::index_format::uint32};
    };
} // namespace rendering_engine
