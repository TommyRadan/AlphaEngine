// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file mesh_draws.hpp
 * @brief The per-frame draw list the scene, depth pre-pass and shadow passes
 *        cull, built once per frame from the render_world's mesh proxies, and
 *        the GPU resources the renderer keeps per proxy to draw it.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <core/math/aabb.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/mesh_proxy.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    struct render_world;

    /**
     * @brief One mesh proxy's part of the frame: its draw, and what the
     *        passes cull it by.
     *
     * There is one per proxy, in the world's proxy order, whether or not it
     * draws this frame, so every pass counts and walks the proxies exactly as
     * they are ordered.
     */
    struct mesh_draw
    {
        // The draw, meaningful only when @ref drawable. Its PerDraw block
        // points into the proxy, which stays put until the frame ends.
        draw_item item{};

        // World bounds, meaningful only when @ref bounded; a proxy without
        // bounds (or one whose material skins its vertices, which the bind
        // pose would not bound) is never culled.
        core::math::aabb bounds{};
        bool bounded{false};

        // The proxy's layer bits.
        uint32_t layer_mask{0};

        // Whether the shadow passes draw it as an occluder (never while its
        // material skins: the depth-only shadow pipelines have no skinned
        // variant).
        bool casts_shadow{false};

        // Whether @ref item is a draw: the proxy is visible, has geometry, a
        // material its vertex format suits, and (when skinned) a palette, and
        // an instanced one has instances to draw.
        bool drawable{false};
    };

    /**
     * @brief Builds the frame's @ref mesh_draw list from a world's mesh
     *        proxies and keeps the GPU resources those draws bind.
     *
     * Owned by the renderer, which calls @ref build once per frame inside the
     * device's frame bracket, before any pass prepares. Everything the draws
     * read comes from the proxies, captured by the render extraction before
     * the frame: the geometry and material, the PerDraw block, the instance
     * snapshot and the joint palette. The per-proxy resources — an instanced
     * proxy's per-instance stream and indirect record, a skinned proxy's
     * joint palette buffer and per-draw bind group — are brought up to date
     * from the proxy here, once per frame, so no pass writes GPU memory
     * while it builds its draws, and are released once their proxy is gone.
     */
    class mesh_draw_builder
    {
    public:
        mesh_draw_builder() = default;
        ~mesh_draw_builder() = default;

        mesh_draw_builder(const mesh_draw_builder&) = delete;
        mesh_draw_builder& operator=(const mesh_draw_builder&) = delete;

        /**
         * @brief Updates the per-proxy resources on @p device from @p world's
         *        proxies and rebuilds the draw list, one entry per proxy.
         */
        void build(render_world& world, gpu::device& device);

        /** @brief This frame's draws, in the world's proxy order. */
        std::span<const mesh_draw> draws() const noexcept
        {
            return m_draws;
        }

        /** @brief Releases every per-proxy resource on @p device. */
        void release(gpu::device& device);

    private:
        // What the renderer keeps for the proxy whose handle has index i,
        // at m_resources[i]; @ref generation tells which proxy of that slot
        // they belong to.
        struct proxy_resources
        {
            uint32_t generation{0};

            // An instanced proxy's per-instance stream (records it has room
            // for) and its indirect record, with the arguments last written
            // into it.
            gpu::buffer instance_buffer{};
            uint32_t instance_capacity{0};
            gpu::buffer indirect_buffer{};
            mesh_indirect_args indirect_args{};
            bool indirect_written{false};

            // A skinned proxy's joint palette (matrices it has room for, and
            // the proxy's palette stamp it holds) and the per-draw group
            // over it, built against @ref skin_layout.
            gpu::buffer joint_buffer{};
            std::size_t joint_capacity{0};
            uint64_t joints_revision{0};
            gpu::bind_group skin_group{};
            gpu::bind_group_layout skin_layout{};
            bool missing_joints_reported{false};
        };

        // Brings @p resources up to date with an instanced proxy's snapshot;
        // false when there is nothing to draw.
        static bool sync_instances(gpu::device& device, mesh_proxy& proxy, proxy_resources& resources);

        // Brings @p resources up to date with a skinned proxy's palette;
        // false when there is nothing to draw with.
        static bool sync_joints(gpu::device& device, const mesh_proxy& proxy, proxy_resources& resources);

        static void release(gpu::device& device, proxy_resources& resources);

        std::vector<proxy_resources> m_resources;
        std::vector<mesh_draw> m_draws;
    };
} // namespace rendering_engine
