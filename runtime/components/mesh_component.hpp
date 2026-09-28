// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file mesh_component.hpp
 * @brief Component that draws a mesh at its node's world transform.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <core/math/aabb.hpp>
#include <core/math/mat4.hpp>
#include <rendering_engine/render_proxies.hpp>

namespace assets
{
    struct mesh_data;
}

namespace rendering_engine
{
    struct material;
    struct mesh_asset;
    struct render_world;
} // namespace rendering_engine

namespace runtime
{
    struct node;

    /**
     * @brief Gives a node a visible mesh.
     *
     * Holds a mesh — a @ref rendering_engine::mesh_asset shared through the
     * asset cache, or mesh data uploaded for this component alone — and the
     * material it draws with, and owns the
     * @ref rendering_engine::mesh_proxy the renderer draws them through,
     * which lives in @c owner.scene()->world() and is named by a handle.
     * @ref on_attach creates the proxy, placed at the node's world
     * transform, and @ref on_destroy — called before the component is freed
     * with the node (or removed) — destroys it. Disabling the node destroys
     * the proxy, enabling it creates it again, last in draw order.
     *
     * The proxy follows the node through @ref extract, which the world's
     * render extraction (@ref runtime::extract_render_proxies) calls once per
     * frame, after every update has moved the nodes: it is placed again only
     * when the node's world matrix changed, and it takes the joint palette a
     * skinning material draws with (@ref set_joint_matrices) only when a new
     * one was set. What the component draws is fixed at construction.
     */
    struct mesh_component
    {
        /** @brief Empty component — draws nothing. */
        mesh_component() = default;

        /**
         * @brief Draws @p mesh with @p material, uploading a private copy of
         *        it right away on the engine's device.
         */
        mesh_component(rendering_engine::material* material, const assets::mesh_data& mesh);

        /**
         * @brief Draws a cached @p mesh with @p material.
         *
         * Shares one GPU upload across every component handed the same
         * @ref rendering_engine::mesh_asset (e.g. many bodies built from one
         * sphere), rather than uploading a private copy per component.
         */
        mesh_component(rendering_engine::material* material, std::shared_ptr<rendering_engine::mesh_asset> mesh);

        /**
         * @brief Draws a cached @p mesh with @p material, and shares
         *        ownership of @p material, so it lives as long as this
         *        component (or a clone of it) draws with it.
         *
         * The form a loaded scene uses (runtime/scene_serializer.hpp): its
         * materials belong to the components that draw with them rather than
         * to the code that built the scene. With a null @p material the
         * component is empty, since it would have nothing to draw with, but
         * @p mesh is still kept (see @ref mesh).
         */
        mesh_component(std::shared_ptr<rendering_engine::material> material,
                       std::shared_ptr<rendering_engine::mesh_asset> mesh);

        /**
         * @brief Creates the proxy in @c owner.scene()->world(), placed at
         *        @p owner, unless the component is empty.
         *
         * Called by @ref node::add_component, once, right after the
         * component is attached; the proxy is the last in draw order.
         */
        void on_attach(node& owner);

        /** @brief Destroys the proxy. Called before the component is freed. */
        void on_destroy();

        /**
         * @brief Destroys the proxy when the owning node is disabled, and
         *        creates it again, last in draw order, when re-enabled.
         *
         * Called by @ref node::set_active.
         */
        void on_active_changed(node& owner, bool active);

        /**
         * @brief Places the proxy when @p owner moved and hands it the joint
         *        palette when a new one was set. Called once per frame by the
         *        render extraction; a component without a proxy does nothing.
         */
        void extract(const node& owner);

        /**
         * @brief A new component drawing the same cached mesh with the same
         *        material, for @c scene::clone.
         *
         * Only a component built from a @ref rendering_engine::mesh_asset can
         * be cloned — the copy shares the upload; one built from a private
         * mesh upload has no source data left to copy, so its clone is empty
         * (with a warning).
         */
        mesh_component clone() const;

        /**
         * @brief Sets the joint palette a skinning material draws the mesh
         *        with: one matrix per joint the vertices' joint indices name,
         *        each mapping the mesh's bind-pose space onto that joint's
         *        current pose in the node's own space (so the node still
         *        places the result).
         *
         * Written each frame by the animation system
         * (@c runtime::animator_component) and handed to the proxy at the
         * next extraction; ignored while the material does not skin. A mesh
         * whose material skins draws nothing until a palette has been set.
         */
        void set_joint_matrices(std::span<const core::math::mat4> matrices);

        /** @brief Joints in the current palette (0 before the first @ref set_joint_matrices). */
        std::size_t joint_count() const noexcept
        {
            return m_joints.size();
        }

        /**
         * @brief Whether the component draws nothing: it was built empty, or
         *        by the loaded-scene form with no material. An empty
         *        component gets no proxy.
         */
        bool is_empty() const noexcept
        {
            return !m_drawn;
        }

        /** @brief The material the mesh draws with, or @c nullptr. */
        rendering_engine::material* material() const noexcept
        {
            return m_material;
        }

        /**
         * @brief The object-space box of the geometry drawn, from CPU-side
         *        data: the cached mesh's bounds, or those of the mesh data a
         *        private upload was built from. @c std::nullopt for an empty
         *        component, or geometry with no vertices.
         */
        std::optional<core::math::aabb> local_bounds() const;

        /**
         * @brief The cached mesh drawn, or @c nullptr for an empty component
         *        or one built from a private upload.
         */
        const std::shared_ptr<rendering_engine::mesh_asset>& mesh() const noexcept
        {
            return m_mesh;
        }

        /** @brief Whether the component was built from mesh data uploaded for it alone. */
        bool has_private_mesh() const noexcept
        {
            return m_private;
        }

        /** @brief The material this component shares ownership of, or @c nullptr when it was handed a plain pointer. */
        const std::shared_ptr<rendering_engine::material>& owned_material() const noexcept
        {
            return m_owned_material;
        }

        /** @brief The proxy in its scene's world; invalid while it has none. */
        rendering_engine::mesh_proxy_handle proxy() const noexcept
        {
            return m_proxy;
        }

    private:
        void create_proxy(const node& owner);
        void destroy_proxy();

        std::shared_ptr<rendering_engine::material> m_owned_material;
        rendering_engine::material* m_material{nullptr};

        // The cached mesh (the second and third constructors), or the
        // private upload and the bounds of the mesh data it was built from
        // (the first).
        std::shared_ptr<rendering_engine::mesh_asset> m_mesh;
        std::shared_ptr<rendering_engine::mesh_asset> m_private_mesh;
        std::optional<core::math::aabb> m_bounds;
        bool m_private{false};

        // Whether the component draws (has a proxy while attached and
        // active); false for an empty one.
        bool m_drawn{false};

        // The joint palette, and a stamp that advances with every write.
        std::vector<core::math::mat4> m_joints;
        uint64_t m_joints_revision{0};

        // The world the proxy lives in (the owning scene's) and the proxy.
        rendering_engine::render_world* m_world{nullptr};
        rendering_engine::mesh_proxy_handle m_proxy{};

        // The node's world-matrix stamp and the palette stamp the proxy was
        // last written from.
        uint64_t m_placed_version{0};
        uint64_t m_written_joints_revision{0};
    };
} // namespace runtime
