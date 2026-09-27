// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <assets/color.hpp>
#include <core/math/vec2.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/materials/ui_material.hpp>
#include <rendering_engine/renderables/premade_2d/rect_transform.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine
{
    /**
     * @brief One textured quad for a @ref sprite_batch, in the terms the
     *        ui vertex shader resolves (see @ref ui_vertex).
     *
     * The pivot lands at @c pivot_anchor * drawable + @c pivot_offset;
     * the quad spans @c corner_min .. @c corner_max around it (each an
     * anchor fraction of the drawable plus pixels, the anchor part
     * non-zero only on stretched axes), turned by @c rotation radians
     * clockwise about the pivot. @c uv_min maps to the corner at
     * @c corner_min.
     */
    struct sprite_quad
    {
        core::math::vec2 pivot_anchor{0.0f, 0.0f};
        core::math::vec2 pivot_offset{0.0f, 0.0f};
        core::math::vec2 corner_min_anchor{0.0f, 0.0f};
        core::math::vec2 corner_min_offset{0.0f, 0.0f};
        core::math::vec2 corner_max_anchor{0.0f, 0.0f};
        core::math::vec2 corner_max_offset{0.0f, 0.0f};
        float rotation{0.0f};
        core::math::vec2 uv_min{0.0f, 0.0f};
        core::math::vec2 uv_max{1.0f, 1.0f};
        assets::color color{255, 255, 255, 255};
    };

    /**
     * @brief Batched 2D quads for the UI pass: one dynamic vertex buffer
     *        and one draw per texture.
     *
     * Quads are queued with @ref add and stay queued until @ref clear, so
     * a batch serves both a retained client that rebuilds its quads only
     * when it changes (@ref label, @ref pane) and an immediate one that
     * clears and refills it every frame. At collect time the quads are
     * grouped by texture in first-use order; each group owns a
     * host-visible vertex buffer (grown in powers of two, rewritten only
     * when its quads changed since the last frame) and a per-draw bind
     * group over its texture, and the batch shares one static index
     * buffer across the groups. A group left empty at collect time is
     * released. All GPU writes happen inside @ref collect_draw_items,
     * i.e. inside the frame bracket, so nothing written races a frame the
     * device still has in flight.
     *
     * Draw order: the groups of one batch draw in first-use order and,
     * since the ui pass keeps the registry's order among draws of one
     * material, a batch registered later paints over one registered
     * earlier.
     *
     * Register the batch (or the client that owns it) with
     * @c renderer::register_ui_renderable, and unregister it before it is
     * destroyed. The textures it draws are borrowed: each must stay alive
     * until the quads using it are cleared and a frame has been collected.
     */
    struct sprite_batch : public renderable
    {
        // @p mat is the ui material the quads draw with (not owned; the
        // renderer's lives until renderer::quit).
        explicit sprite_batch(ui_material* mat);
        ~sprite_batch() override;

        sprite_batch(const sprite_batch&) = delete;
        sprite_batch& operator=(const sprite_batch&) = delete;

        /** @brief Drops every queued quad; the GPU buffers are kept for reuse. */
        void clear();

        /**
         * @brief Queues @p quad sampling @p texture; an invalid handle
         *        samples the material's white texel, i.e. draws the
         *        flat vertex colour.
         */
        void add(gpu::texture texture, const sprite_quad& quad);

        /**
         * @brief Queues a quad covering @p rect in the drawable, sampling
         *        @p uv_min .. @p uv_max of @p texture (invalid: a flat
         *        @p color) modulated by @p color. Only a top-level rect —
         *        one whose parent is the drawable — resolves correctly.
         */
        void add(gpu::texture texture,
                 const rect_transform& rect,
                 const assets::color& color,
                 const core::math::vec2& uv_min = core::math::vec2{0.0f, 0.0f},
                 const core::math::vec2& uv_max = core::math::vec2{1.0f, 1.0f});

        /** @brief Quads queued, over every texture. */
        std::size_t quad_count() const;

        /** @brief Nothing to do up front: buffers are created on first collect. */
        void upload() final;

        void collect_draw_items(std::vector<draw_item>& out) final;

        bool casts_shadow() const override
        {
            return false;
        }

    private:
        struct texture_group
        {
            gpu::texture texture{};
            std::vector<ui_vertex> vertices;
            gpu::buffer vertex_buffer{};
            std::size_t capacity{0}; // quads the vertex buffer holds
            gpu::bind_group bind_group{};
            bool dirty{true};
        };

        texture_group& group_for(gpu::texture texture);
        void release(texture_group& group);

        // Grows the shared index buffer to hold at least @p quads quads.
        void reserve_indices(std::size_t quads);

        ui_material* m_material{nullptr};
        std::vector<texture_group> m_groups;
        gpu::buffer m_index_buffer{};
        std::size_t m_index_capacity{0}; // quads the index buffer covers
        std::size_t m_quad_count{0};
    };
} // namespace rendering_engine
