// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file renderable_component.hpp
 * @brief Component that draws a mesh source at its node.
 */

#pragma once

#include <concepts>
#include <cstdint>
#include <memory>
#include <utility>

#include <rendering_engine/render_proxies.hpp>
#include <rendering_engine/renderables/mesh_source.hpp>

namespace rendering_engine
{
    struct render_world;
}

namespace runtime
{
    struct node;

    /**
     * @brief Gives a node a mesh source — a @c line, a @c points cloud, an
     *        @c instanced_mesh.
     *
     * The @ref mesh_component counterpart for sources that build their own
     * geometry: it owns the @ref rendering_engine::mesh_source on the heap
     * and the @ref rendering_engine::mesh_proxy the renderer draws it
     * through, which lives in @c owner.scene()->world() and is named by a
     * handle. @ref on_attach creates the proxy, placed at the node's world
     * pose, and @ref on_destroy — called before the component is freed with
     * the node (or removed) — destroys it. Disabling the node destroys the
     * proxy, enabling it creates it again, last in draw order.
     *
     * The proxy follows the node and the source through @ref extract, which
     * the world's render extraction (@ref runtime::extract_render_proxies)
     * calls once per frame, after every update has moved the nodes: it is
     * placed again only when the node's world matrix changed, and described
     * again (with, for an instanced source, the instance records changed
     * since) only when the source's revision moved. An instanced source's
     * instances carry world transforms, so it ignores the node's pose.
     *
     * The source is ready to draw once built (its geometry is uploaded when
     * set). Move-only, and not cloneable (a source cannot be copied
     * generically): a clone of the node leaves it off, with the store's
     * warning.
     */
    struct renderable_component
    {
        /** @brief Empty component — owns and draws nothing. */
        renderable_component() = default;

        /** @brief Takes ownership of @p source, which gets its proxy on attach. */
        template<typename S>
            requires std::derived_from<S, rendering_engine::mesh_source>
        explicit renderable_component(std::unique_ptr<S> source) : m_source{std::move(source)}
        {
        }

        /** @brief Creates the source's proxy in @c owner.scene()->world(), placed at @p owner. */
        void on_attach(node& owner);

        /** @brief Destroys the source's proxy. Called before the component is freed. */
        void on_destroy();

        /** @brief Creates (@p active) or destroys the proxy with its node's active state. */
        void on_active_changed(node& owner, bool active);

        /**
         * @brief Places the proxy when @p owner moved and describes the
         *        source again when it changed. Called once per frame by the
         *        render extraction; a component without a proxy does nothing.
         */
        void extract(const node& owner);

        /** @brief The owned source, or @c nullptr for an empty component. */
        rendering_engine::mesh_source* get() const noexcept
        {
            return m_source.get();
        }

        /** @brief The owned source as an @c S, or @c nullptr if it is not one (or there is none). */
        template<typename S>
        S* get_as() const noexcept
        {
            return dynamic_cast<S*>(m_source.get());
        }

        /** @brief The source's proxy in its scene's world; invalid while it has none. */
        rendering_engine::mesh_proxy_handle proxy() const noexcept
        {
            return m_proxy;
        }

    private:
        void create_proxy(const node& owner);
        void destroy_proxy();

        std::unique_ptr<rendering_engine::mesh_source> m_source;

        // The world the proxy lives in (the owning scene's) and the proxy.
        rendering_engine::render_world* m_world{nullptr};
        rendering_engine::mesh_proxy_handle m_proxy{};

        // The node's world-matrix stamp and the source's revision the proxy
        // was last written from.
        uint64_t m_placed_version{0};
        uint64_t m_described_revision{0};
    };
} // namespace runtime
