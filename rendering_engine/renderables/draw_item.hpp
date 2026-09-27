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
#include <span>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine
{
    struct material;

    // One draw the pass can dispatch. Renderables fill this struct in
    // @ref renderable::collect_draw_items; the pass then sorts the
    // collected items by (pipeline id, material instance) and walks
    // them, issuing @c set_pipeline only when the pipeline changes and
    // rebinding the per-material group only when the instance changes.
    // An invalid @ref index_buffer means non-indexed draw — the pass
    // calls @c draw(vertex_count) instead of @c draw_indexed(index_count).
    //
    // @ref mirrored is set by the renderable when its model matrix has a
    // negative determinant (see @ref is_mirrored): the transform
    // reverses every triangle's winding, so the pass draws the item with
    // @c mat->pipeline(true), the same variant with a clockwise front
    // face, and the mesh's outside stays visible.
    //
    // A valid @ref indirect_buffer turns the indexed draw into an
    // indexed *indirect* draw: the pass binds the index buffer and
    // calls @c draw_indexed_indirect, sourcing the index count and the
    // instance count from the buffer's @c DrawElementsIndirectCommand
    // record instead of from @ref index_count. This is how
    // @ref instanced_mesh emits a single instanced draw over shared
    // geometry; only the index path supports it.
    //
    // A valid @ref instance_buffer is bound to vertex slot 1 as the
    // per-instance stream (one record per instanced draw copy, stepped
    // by the pipeline's per-instance vertex layout); @ref instance_stride
    // is its record size. Used together with @ref indirect_buffer by
    // @ref instanced_mesh.
    //
    // @ref per_draw_bind_group is bound at the material's per-draw slot.
    // For a rigid 3D renderable it is the per-draw ring's group shared by
    // every draw under that layout, and @ref per_draw_offset selects the
    // renderable's block within it: the pass hands
    // @ref per_draw_offsets to @c set_bind_group, which carries the offset
    // only while @ref per_draw_dynamic says the group's layout takes one
    // (a sprite batch's texture group or a skinned draw's private group
    // does not).
    //
    // @ref first_index and @ref vertex_offset address a sub-range of the
    // bound geometry, so several meshes can live in one vertex / index
    // buffer: an indexed draw reads @ref index_count indices from
    // @ref first_index and adds @ref vertex_offset (the base vertex) to
    // each; a non-indexed draw reads @ref vertex_count vertices from
    // vertex @ref vertex_offset, which must then not be negative. Both are
    // ignored by the indirect path, whose record carries its own.
    struct draw_item
    {
        material* mat{nullptr};
        gpu::buffer vertex_buffer{};
        gpu::buffer index_buffer{};
        gpu::buffer indirect_buffer{};
        gpu::buffer instance_buffer{};
        gpu::bind_group per_draw_bind_group{};
        uint32_t vertex_count{0};
        uint32_t index_count{0};
        uint32_t vertex_stride{0};
        uint32_t instance_stride{0};
        // Instances drawn by this item: what a direct draw is issued with,
        // 1 for ordinary draws. An indirect draw takes its count from the
        // command record instead; the renderable mirrors that count here
        // so render-stats accounting matches what the GPU draws.
        uint32_t instance_count{1};
        gpu::index_format index_format{gpu::index_format::uint32};
        uint32_t first_index{0};
        int32_t vertex_offset{0};
        // Byte offset of this draw's PerDraw block in the buffer behind
        // @ref per_draw_bind_group; see above.
        uint32_t per_draw_offset{0};
        bool per_draw_dynamic{false};
        // Whether the model matrix flips handedness; see above.
        bool mirrored{false};

        // The dynamic offsets to bind @ref per_draw_bind_group with: the
        // one per-draw offset for a group over the per-draw ring, none
        // for a group whose layout takes no dynamic offset.
        std::span<const uint32_t> per_draw_offsets() const noexcept
        {
            return per_draw_dynamic ? std::span<const uint32_t>{&per_draw_offset, 1} : std::span<const uint32_t>{};
        }
    };
} // namespace rendering_engine
