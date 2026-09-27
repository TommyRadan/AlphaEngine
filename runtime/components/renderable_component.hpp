// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file renderable_component.hpp
 * @brief Component that draws any scene renderable at its node.
 */

#pragma once

#include <concepts>
#include <memory>
#include <utility>

#include <core/math/transform.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace runtime
{
    struct node;

    /**
     * @brief Gives a node a ready-made renderable — a @c premade_3d
     *        primitive, a @c line, a @c points cloud, an @c instanced_mesh.
     *
     * The @ref mesh_component counterpart for renderables that build their own
     * geometry: it owns the renderable on the heap and, while attached,
     * registers it with the scene renderer, so destroying the node (or
     * removing the component) unregisters and frees it. A renderable with a
     * public @c transform member has that transform parented under the node
     * on attach, so it draws at the node's world pose with its own transform
     * as a local offset (identity unless the caller set one); one without
     * (an @c instanced_mesh, whose instances carry world transforms) is drawn
     * as it is. Disabling the node unregisters the renderable, enabling it
     * registers it again.
     *
     * The renderable must be ready to draw — @c upload() called — before it
     * is attached. The heap address is what the renderer holds, so it
     * survives the component being relocated within its pool. Move-only, and
     * not cloneable (a renderable cannot be copied generically): a clone of
     * the node leaves it off, with the store's warning.
     */
    struct renderable_component
    {
        /** @brief Empty component — owns and draws nothing. */
        renderable_component() = default;

        /** @brief Takes ownership of @p renderable (see the class notes). */
        template<typename R>
            requires std::derived_from<R, rendering_engine::renderable>
        explicit renderable_component(std::unique_ptr<R> renderable)
        {
            if constexpr (requires(R& r) {
                              { &r.transform } -> std::convertible_to<core::transform*>;
                          })
            {
                if (renderable)
                {
                    m_transform = &renderable->transform;
                }
            }
            m_renderable = std::move(renderable);
        }

        /** @brief Parents the renderable's transform under @p owner and registers it with the scene renderer. */
        void on_attach(node& owner);

        /** @brief Unregisters the renderable and unparents its transform. */
        void on_destroy();

        /** @brief Registers (@p active) or unregisters the renderable with its node's active state. */
        void on_active_changed(node& owner, bool active);

        /** @brief The owned renderable, or @c nullptr for an empty component. */
        rendering_engine::renderable* get() const noexcept
        {
            return m_renderable.get();
        }

        /** @brief The owned renderable as an @c R, or @c nullptr if it is not one (or there is none). */
        template<typename R>
        R* get_as() const noexcept
        {
            return dynamic_cast<R*>(m_renderable.get());
        }

    private:
        void register_renderable();
        void unregister_renderable();

        std::unique_ptr<rendering_engine::renderable> m_renderable;
        // The renderable's own transform, when it has one; points into the
        // heap object, so it stays valid as the component moves.
        core::transform* m_transform{nullptr};
        bool m_registered{false};
    };
} // namespace runtime
