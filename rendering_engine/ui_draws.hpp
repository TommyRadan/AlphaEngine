// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file ui_draws.hpp
 * @brief The per-frame UI draw list, built once per frame from the
 *        render_world's UI proxies, and the GPU resources the renderer keeps
 *        per proxy to draw it.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    struct material;
    struct render_world;
    struct ui_proxy;

    /**
     * @brief Builds the frame's UI draws from a world's UI proxies and keeps
     *        the GPU resources those draws bind.
     *
     * Owned by the renderer, which calls @ref build once per frame inside the
     * device's frame bracket, before any pass prepares. Each quad group of a
     * proxy is one draw, in the proxy's group order, and the proxies follow
     * each other in paint order. Per group the builder keeps a host-visible
     * vertex buffer (grown in powers of two) and a per-draw bind group over
     * the group's texture, and per proxy one index buffer shared by its
     * groups; a proxy's quads are uploaded again only when its revision
     * moved, and its resources are released once the proxy is gone.
     */
    class ui_draw_builder
    {
    public:
        ui_draw_builder() = default;
        ~ui_draw_builder() = default;

        ui_draw_builder(const ui_draw_builder&) = delete;
        ui_draw_builder& operator=(const ui_draw_builder&) = delete;

        /**
         * @brief Updates the per-proxy resources on @p device from @p world's
         *        UI proxies and rebuilds the draw list.
         */
        void build(const render_world& world, gpu::device& device);

        /** @brief This frame's UI draws, in paint order. */
        std::span<const draw_item> draws() const noexcept
        {
            return m_draws;
        }

        /** @brief Releases every per-proxy resource on @p device. */
        void release(gpu::device& device);

    private:
        // One quad group's vertex buffer (quads it has room for) and the
        // per-draw group over the texture it was built for.
        struct group_resources
        {
            gpu::texture texture{};
            gpu::buffer vertex_buffer{};
            std::size_t capacity{0};
            gpu::bind_group bind_group{};
        };

        // What the renderer keeps for the proxy whose handle has index i,
        // at m_resources[i]; @ref generation tells which proxy of that slot
        // they belong to, @ref revision which of its writes was uploaded.
        struct proxy_resources
        {
            uint32_t generation{0};
            uint64_t revision{0};
            const material* mat{nullptr};
            std::vector<group_resources> groups;
            gpu::buffer index_buffer{};
            std::size_t index_capacity{0};
        };

        // Brings @p resources up to date with @p proxy's quads.
        static void sync(gpu::device& device, const ui_proxy& proxy, proxy_resources& resources);

        // Grows @p resources' index buffer to cover at least @p quads quads.
        static void reserve_indices(gpu::device& device, proxy_resources& resources, std::size_t quads);

        static void release(gpu::device& device, group_resources& group);
        static void release(gpu::device& device, proxy_resources& resources);

        std::vector<proxy_resources> m_resources;
        std::vector<draw_item> m_draws;
    };
} // namespace rendering_engine
