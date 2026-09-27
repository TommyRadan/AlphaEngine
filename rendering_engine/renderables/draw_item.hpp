// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <bit>
#include <cstdint>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine
{
    struct material;
    struct per_draw_payload;

    // The render queue a draw item's @ref draw_item::sort_key buckets it
    // into: opaque draws sort front-to-back so the depth test rejects
    // hidden fragments early, transparent draws sort back-to-front so
    // blending composites in the right order. The values occupy the top
    // byte of the sort key, leaving room to add more queues later (e.g. a
    // screen-space overlay queue that always draws last) without
    // reshuffling the two that exist.
    enum class render_queue : uint8_t
    {
        opaque = 0,
        transparent = 1,
    };

    // Packs @p queue, the item's @p view_depth (view-space depth — camera-
    // forward positive — to its world bounds centre, or 0 for a renderable
    // that reports no bounds) and @p pipeline_id into the 64-bit key a
    // pass sorts draw_item by: [63:56] the queue, [55:24] a monotonic
    // 32-bit encoding of the depth, [23:0] the pipeline id. A negative
    // depth (behind the camera) is clamped to 0 first, so the encoding —
    // the depth float's raw bit pattern, which orders the same as the
    // float for every non-negative value — stays order-preserving. The
    // transparent queue's depth bits are then bitwise complemented, so a
    // plain ascending sort over the whole key still gives each queue the
    // order it wants: opaque's bits rise with depth (nearest first),
    // transparent's complemented bits fall with depth (farthest first).
    inline uint64_t make_sort_key(render_queue queue, float view_depth, uint64_t pipeline_id) noexcept
    {
        const float clamped_depth = view_depth > 0.0f ? view_depth : 0.0f;
        uint32_t depth_bits = std::bit_cast<uint32_t>(clamped_depth);
        if (queue == render_queue::transparent)
        {
            depth_bits = ~depth_bits;
        }
        return (uint64_t{static_cast<uint8_t>(queue)} << 56) | (uint64_t{depth_bits} << 24) | (pipeline_id & 0xFFFFFFu);
    }

    // One draw the pass can dispatch. Renderables fill this struct in
    // @ref renderable::collect_draw_items; a pass that cares about draw
    // order (the scene pass) fills @ref sort_key from it afterwards and
    // sorts the collected items by that key, falling back to the material
    // instance on a tie, then walks them, issuing @c set_pipeline only
    // when the pipeline changes and rebinding the per-material group only
    // when the instance changes; a pass that does not build a sort key
    // (the ui and debug passes) sorts by (pipeline id, material instance)
    // directly instead. An invalid @ref index_buffer means non-indexed
    // draw — the pass calls @c draw(vertex_count) instead of
    // @c draw_indexed(index_count).
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
    // @ref per_draw_push, when set, is the draw's PerDraw block (see
    // per_draw_ubo.hpp): the pass pushes it right before the draw as
    // push constants. It points at the block the renderable caches,
    // which stays put until the renderable collects again, so it stays
    // valid while the frame records from the list it was collected into
    // (the depth pre-pass and the scene pass share one); the bytes are
    // copied when pushed.
    //
    // @ref per_draw_bind_group, when valid, is bound at the material's
    // per-draw slot: a sprite batch's texture group, or a skinned draw's
    // private group carrying its joint palette. A rigid 3D draw has none.
    // @ref bind_per_draw records the push and the group.
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
        // The PerDraw block the pass pushes, or null; see above.
        const per_draw_payload* per_draw_push{nullptr};
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
        // Whether the model matrix flips handedness; see above.
        bool mirrored{false};

        // Sort key a pass computes for this item with @ref make_sort_key
        // once every field above is filled: the render queue, a depth term
        // and the pipeline id packed into one 64-bit word so a single
        // ascending @c std::stable_sort orders a frame's items. Zero — the
        // opaque queue at zero depth — until such a pass fills it; the ui
        // and debug passes leave it at that and sort by pipeline and
        // material instance directly instead.
        uint64_t sort_key{0};
    };
} // namespace rendering_engine
