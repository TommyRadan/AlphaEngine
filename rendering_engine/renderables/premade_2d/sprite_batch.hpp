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
#include <rendering_engine/renderables/premade_2d/ui_element.hpp>
#include <rendering_engine/ui_proxy.hpp>

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
     * @brief Batched 2D quads for the UI pass: one draw per texture.
     *
     * Quads are queued with @ref add and stay queued until @ref clear, so
     * a batch serves both a retained client that rebuilds its quads only
     * when it changes (@ref label, @ref pane) and an immediate one that
     * clears and refills it every frame. The quads are grouped by texture
     * in first-use order, and @ref capture hands them over in that order;
     * a texture nothing was queued for since the last @ref clear is dropped
     * from the order there. The batch holds only CPU data: the renderer
     * uploads what its UI proxy captured, inside the frame bracket.
     *
     * Draw order: the groups of one batch draw in first-use order and,
     * since the ui pass keeps the proxies' order among draws of one
     * material, an element whose proxy was created later paints over one
     * created earlier. The textures it draws are borrowed: each must stay
     * alive until the quads using it are cleared and captured again.
     */
    struct sprite_batch : public ui_element
    {
        // @p mat is the ui material the quads draw with (not owned; the
        // renderer's lives until renderer::quit).
        explicit sprite_batch(ui_material* mat);

        /** @brief Drops every queued quad. */
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

        /** @brief The queued quads, one group per texture in first-use order. */
        ui_element_data capture() override;

    private:
        ui_quad_group& group_for(gpu::texture texture);

        ui_material* m_material{nullptr};
        std::vector<ui_quad_group> m_groups;
        std::size_t m_quad_count{0};
    };
} // namespace rendering_engine
