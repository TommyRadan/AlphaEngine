// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file ui_element_component.hpp
 * @brief Component that draws a UI element over the frame.
 */

#pragma once

#include <concepts>
#include <cstdint>
#include <memory>
#include <utility>

#include <rendering_engine/render_proxies.hpp>
#include <rendering_engine/renderables/premade_2d/ui_element.hpp>

namespace rendering_engine
{
    struct render_world;
}

namespace runtime
{
    struct node;

    /**
     * @brief Gives a node a UI element — a @c pane, a @c label, a
     *        @c sprite_batch — drawn by the UI pass in the drawable's pixel
     *        space.
     *
     * Owns the @ref rendering_engine::ui_element on the heap and the
     * @ref rendering_engine::ui_proxy the renderer draws it through, which
     * lives in @c owner.scene()->world() and is named by a handle.
     * @ref on_attach creates the proxy, over every UI element created before
     * it, and @ref on_destroy — called before the component is freed with
     * the node (or removed) — destroys it. Disabling the node destroys the
     * proxy, enabling it creates it again, on top. The element is placed by
     * its own rect, not by the node.
     *
     * The proxy follows the element through @ref extract, which the world's
     * render extraction (@ref runtime::extract_render_proxies) calls once per
     * frame: the element's quads are captured again only when its revision
     * moved. Move-only, and not cloneable (an element cannot be copied
     * generically): a clone of the node leaves it off, with the store's
     * warning.
     */
    struct ui_element_component
    {
        /** @brief Empty component — owns and draws nothing. */
        ui_element_component() = default;

        /** @brief Takes ownership of @p element, which gets its proxy on attach. */
        template<typename E>
            requires std::derived_from<E, rendering_engine::ui_element>
        explicit ui_element_component(std::unique_ptr<E> element) : m_element{std::move(element)}
        {
        }

        /** @brief Creates the element's proxy in @c owner.scene()->world(). */
        void on_attach(node& owner);

        /** @brief Destroys the element's proxy. Called before the component is freed. */
        void on_destroy();

        /** @brief Creates (@p active) or destroys the proxy with its node's active state. */
        void on_active_changed(node& owner, bool active);

        /**
         * @brief Captures the element into its proxy when it changed. Called
         *        once per frame by the render extraction; a component
         *        without a proxy does nothing.
         */
        void extract(const node& owner);

        /** @brief The owned element, or @c nullptr for an empty component. */
        rendering_engine::ui_element* get() const noexcept
        {
            return m_element.get();
        }

        /** @brief The owned element as an @c E, or @c nullptr if it is not one (or there is none). */
        template<typename E>
        E* get_as() const noexcept
        {
            return dynamic_cast<E*>(m_element.get());
        }

        /** @brief The element's proxy in its scene's world; invalid while it has none. */
        rendering_engine::ui_proxy_handle proxy() const noexcept
        {
            return m_proxy;
        }

    private:
        void create_proxy();
        void destroy_proxy();

        std::unique_ptr<rendering_engine::ui_element> m_element;

        // The world the proxy lives in (the owning scene's) and the proxy.
        rendering_engine::render_world* m_world{nullptr};
        rendering_engine::ui_proxy_handle m_proxy{};

        // The element's revision the proxy was last written from.
        uint64_t m_captured_revision{0};
    };
} // namespace runtime
