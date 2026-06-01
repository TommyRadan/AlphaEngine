/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file node.hpp
 * @brief Scene-graph node — the entity in the entity/component model.
 */

#pragma once

#include <string>
#include <typeindex>
#include <vector>

#include <infrastructure/log.hpp>
#include <infrastructure/math/math.hpp>
#include <rendering_engine/util/transform.hpp>
#include <scene_graph/component.hpp>

namespace scene_graph
{
    /**
     * @brief A node in the scene hierarchy — the entity of the
     *        entity/component model.
     *
     * Every node has two intrinsic things: a local @ref transform (its pose
     * relative to its parent) and a set of parent/child links, so world
     * matrices propagate down the tree the way @c THREE.Object3D /
     * @c GameObject hierarchies do. Everything else a node "is" — a mesh, a
     * camera, a light, an audio emitter — is expressed by attaching
     * **components**. A node carrying no components is an empty group used to
     * move a subtree as a unit.
     *
     * Components are not stored in the node. The node records, per component,
     * a @ref component_handle into a pool owned by the scene's
     * @ref component_store (set via @ref set_store, normally inherited from the
     * parent on @ref add). The node owns the *handle* and drives the
     * component's lifetime — destroying the node, or calling
     * @ref remove_component, frees the pooled storage — while the store (the
     * subsystem) owns the *data*. This keeps component data contiguous in its
     * subsystem and lets nodes stay small.
     *
     * Links are non-owning raw pointers: a node never deletes its parent or
     * children, and the caller keeps every node alive while it is wired into a
     * tree. The destructor detaches from the parent, orphans children back to
     * world space, and frees this node's components. Main-thread-only.
     */
    struct node
    {
        node();
        ~node();

        // Non-copyable: a node holds raw parent/child links and component
        // handles that a copy would alias. Moving would invalidate the
        // addresses children parent against.
        node(const node&) = delete;
        node& operator=(const node&) = delete;
        node(node&&) = delete;
        node& operator=(node&&) = delete;

        /**
         * @brief Local transform, relative to the parent node (or world space
         *        when this node is a root).
         *
         * Its parent pointer is kept in sync with @ref add / @ref remove, so
         * @c transform.get_world_matrix() and @ref world_matrix agree.
         */
        rendering_engine::util::transform transform;

        /** @brief Optional label, used by @ref find. Not required to be unique. */
        std::string name;

        /**
         * @brief Returns the first node in this subtree (this node included)
         *        whose @ref name equals @p target, or @c nullptr.
         *
         * Depth-first, in child insertion order.
         */
        node* find(const std::string& target);

        // --- World-space helpers -------------------------------------------

        /** @brief This node's world-space position (translation of @ref world_matrix). */
        infrastructure::math::vec3 world_position() const;

        /**
         * @brief Places the node at @p world_position in world space by solving
         *        for the local position under the current parent.
         */
        void set_world_position(const infrastructure::math::vec3& world_position);

        /**
         * @brief Orients the node so its forward axis points at @p target in
         *        world space. Exact when ancestors are unrotated/unscaled; under
         *        a rotated parent the @p up handling is approximate.
         */
        void look_at(const infrastructure::math::vec3& target,
                     const infrastructure::math::vec3& up = infrastructure::math::vec3{0.0f, 0.0f, 1.0f});

        // --- Active / visible state ----------------------------------------

        /** @brief This node's own active flag (ignores ancestors). */
        bool is_active() const noexcept;

        /** @brief True when this node and every ancestor are active. */
        bool is_effective_active() const noexcept;

        /**
         * @brief Enables or disables this node (and, by inheritance, its subtree).
         *
         * A disabled subtree is skipped by @ref update_subtree and its components
         * are told to hide via @c on_active_changed (e.g. a @c mesh_component
         * unregisters its model from the renderer), so it stops both updating and
         * drawing. Re-enabling restores it, provided every ancestor is active.
         */
        void set_active(bool active);

        /**
         * @brief Attaches @p child below this node.
         *
         * Re-parents @p child (detaching it from any previous parent first),
         * points its transform at this node so world matrices propagate, and
         * — if @p child has no store yet — hands it this node's store so it
         * can carry components. A node is never added to itself.
         */
        void add(node& child);

        /** @brief Detaches @p child, returning it to world space. No-op if not a child. */
        void remove(node& child);

        /** @brief Parent node, or @c nullptr when this node is a root. */
        node* parent() const noexcept;

        /** @brief Direct children, in insertion order. */
        const std::vector<node*>& children() const noexcept;

        /**
         * @brief Updates this node's components, then recurses into children.
         *
         * Calls each component's @c on_update(node&) (those that define one) so
         * components can resync from the node's now-settled world transform.
         * Driven once per frame from @ref scene_graph::context::update on the
         * scene root, after game-module @c on_frame has moved nodes and before
         * the renderer walks the frame.
         */
        void update_subtree();

        /**
         * @brief World-space matrix of this node.
         *
         * Equivalent to @c transform.get_world_matrix(): @c parent.world * local
         * when parented, otherwise the local matrix.
         */
        infrastructure::math::mat4 world_matrix() const;

        /**
         * @brief Sets the component store this node draws its component pools
         *        from, propagating to children that have none.
         *
         * Usually called indirectly: a scene's root is given the store by
         * @ref scene_graph::context, and @ref add hands it down the tree.
         */
        void set_store(component_store* store) noexcept;

        /** @brief Component store backing this node, or @c nullptr if unscoped. */
        component_store* store() const noexcept;

        /**
         * @brief Adds (or replaces) the @c C component on this node.
         *
         * Stores @p value in the scene's pool for @c C and records its handle.
         * Replaces any existing @c C on this node. Returns a pointer to the
         * pooled component, or @c nullptr if the node has no store yet.
         */
        template<typename C>
        C* add_component(C value)
        {
            if (m_store == nullptr)
            {
                LOG_WRN("scene_graph::node::add_component: node has no component store");
                return nullptr;
            }

            remove_component<C>();
            component_handle handle = m_store->add<C>(std::move(value));
            m_components.push_back(component_entry{std::type_index(typeid(C)), handle});

            C* component = m_store->get<C>(handle);
            // Components that bridge to a subsystem (e.g. registering a
            // renderable) wire themselves up here, now that they know their
            // owning node. Plain-data components define no on_attach and skip
            // this. The pooled component may be relocated later, so on_attach
            // must key any external registration off stable state (its owned
            // heap object / the node's transform), not its own address.
            if constexpr (requires(C& c, node& n) { c.on_attach(n); })
            {
                if (component != nullptr)
                {
                    component->on_attach(*this);
                }
            }
            // If the node is currently disabled, hide the freshly attached
            // component so it matches the subtree's visibility.
            if (!m_effective_active)
            {
                m_store->set_active(std::type_index(typeid(C)), handle, *this, false);
            }
            return component;
        }

        /** @brief Pointer to this node's @c C component, or @c nullptr if it has none. */
        template<typename C>
        C* get_component() noexcept
        {
            if (m_store == nullptr)
            {
                return nullptr;
            }
            std::type_index type{typeid(C)};
            for (const component_entry& entry : m_components)
            {
                if (entry.type == type)
                {
                    return m_store->get<C>(entry.handle);
                }
            }
            return nullptr;
        }

        /** @brief True when this node carries a @c C component. */
        template<typename C>
        bool has_component() const noexcept
        {
            std::type_index type{typeid(C)};
            for (const component_entry& entry : m_components)
            {
                if (entry.type == type)
                {
                    return true;
                }
            }
            return false;
        }

        /** @brief Removes this node's @c C component and frees its pooled storage. No-op if absent. */
        template<typename C>
        void remove_component() noexcept
        {
            std::type_index type{typeid(C)};
            for (auto it = m_components.begin(); it != m_components.end(); ++it)
            {
                if (it->type == type)
                {
                    if (m_store != nullptr)
                    {
                        m_store->remove<C>(it->handle);
                    }
                    m_components.erase(it);
                    return;
                }
            }
        }

    private:
        struct component_entry
        {
            std::type_index type;
            component_handle handle;
        };

        // Recomputes effective-active from @p parent_effective, dispatching
        // on_active_changed to this node's components when it flips, and recurses
        // into children with the new value.
        void refresh_active(bool parent_effective);

        node* m_parent;
        std::vector<node*> m_children;
        component_store* m_store;
        std::vector<component_entry> m_components;

        bool m_active;
        bool m_effective_active;
    };
} // namespace scene_graph
