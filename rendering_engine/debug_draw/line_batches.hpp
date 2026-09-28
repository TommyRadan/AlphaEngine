// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file line_batches.hpp
 * @brief The two line proxies a world's debug-draw list is drawn through.
 */

#pragma once

#include <memory>
#include <vector>

#include <core/math/vec3.hpp>
#include <rendering_engine/render_proxies.hpp>

namespace rendering_engine
{
    struct line;
    struct line_material;
    struct material;
    struct material_library;
    struct render_world;

    namespace gpu
    {
        struct device;
    }
} // namespace rendering_engine

namespace rendering_engine::debug_draw
{
    /**
     * @brief Turns a @ref render_world's debug-draw list into the line
     *        proxies the passes draw: every on-top segment in one overlay
     *        proxy the debug pass draws last, every depth-tested one in one
     *        scene proxy the scene pass draws with the rest of the world.
     *
     * The renderer owns one in Debug builds and calls @ref capture at the
     * start of every frame, before the frame opens, once everything this
     * frame draws has been recorded. Each batch is one line geometry in
     * world space, at the identity placement, on @ref layer_editor, with no
     * bounds (never culled) and no shadow; it is uploaded again only when
     * its segments changed since the last frame, and it has geometry and a
     * proxy only while it has segments, so a frame with nothing
     * depth-tested adds nothing to the scene pass. The on-top batch draws
     * with the depth-less debug line material; the depth-tested one with a
     * line material of its own that tests depth without writing it, built
     * the first time it is needed. Destroying the batches destroys their
     * proxies.
     */
    struct line_batches
    {
        /**
         * @brief Batches of @p world's debug-draw list that upload to
         *        @p device and draw with @p materials' line materials; all
         *        three outlive the batches.
         */
        line_batches(render_world& world, gpu::device& device, material_library& materials);
        ~line_batches();

        line_batches(const line_batches&) = delete;
        line_batches& operator=(const line_batches&) = delete;

        /**
         * @brief Brings the proxies up to date with the world's debug-draw
         *        list. Between frames only.
         */
        void capture();

    private:
        struct batch
        {
            std::unique_ptr<rendering_engine::line> geometry;
            material* mat{nullptr};
            mesh_proxy_handle proxy{};
            // What the geometry was last uploaded from.
            std::vector<core::math::vec3> positions;
            std::vector<core::math::vec3> colors;
        };

        void capture(batch& target, bool depth_test);
        void clear(batch& target);

        render_world* m_world{nullptr};
        gpu::device* m_device{nullptr};
        material_library* m_materials{nullptr};
        std::unique_ptr<line_material> m_depth_tested_material;
        batch m_on_top;
        batch m_depth_tested;

        // Scratch the list is flattened into every frame.
        std::vector<core::math::vec3> m_positions;
        std::vector<core::math::vec3> m_colors;
    };
} // namespace rendering_engine::debug_draw
