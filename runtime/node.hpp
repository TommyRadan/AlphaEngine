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

#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <vector>

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/util/transform.hpp>
#include <runtime/component.hpp>

namespace runtime
{
    struct context;

    /**
     * @brief A node in the scene hierarchy — the entity of the
     *        entity/component model.
     *
     * Every node has two intrinsic things: a local @ref transform (its pose
     * relative to its parent) and a set of parent/child links, so world
     * matrices propagate down the tree the way @c GameObject
     * hierarchies do. Everything else a node "is" — a mesh, a
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
     *
     * **Structural mutation during a traversal.** While the scene is walking
     * the tree — inside a component's @c on_update (from
     * @ref update_subtree) or @c on_active_changed (from @ref set_active) —
     * the node and child lists being iterated must not change. The immediate
     * APIs (@ref add, @ref remove, @ref set_active, @ref add_component,
     * @ref remove_component, @ref remove_all_components) detect that case
     * through the owning @ref runtime::context: in debug builds they assert;
     * in release builds they log an error and apply the call at the end of
     * @ref runtime::context::update instead. Code that needs to mutate the
     * tree from a hook should say so explicitly with the scene's
     * @c defer_destroy / @c defer_remove_component / @c defer_reparent /
     * @c defer_set_active, reached via @ref scene.
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
        core::math::vec3 world_position() const;

        /**
         * @brief Places the node at @p world_position in world space by solving
         *        for the local position under the current parent.
         */
        void set_world_position(const core::math::vec3& world_position);

        /**
         * @brief Orients the node so its forward axis points at @p target in
         *        world space. Exact when ancestors are unrotated/unscaled; under
         *        a rotated parent the @p up handling is approximate.
         */
        void look_at(const core::math::vec3& target, const core::math::vec3& up = core::math::vec3{0.0f, 0.0f, 1.0f});

        // --- Active / visible state ----------------------------------------

        /** @brief This node's own active flag (ignores ancestors). */
        bool is_active() const noexcept;

        /** @brief True when this node and every ancestor are active. */
        bool is_effective_active() const noexcept;

        /**
         * @brief Enables or disables this node (and, by inheritance, its subtree).
         *
         * A disabled subtree is skipped by @ref update_subtree and its components
         * are told to hide via @c on_active_changed (a @c mesh_component
         * unregisters its model, a @c light_component takes its light out of
         * the registry, a @c camera_component detaches its camera), so it stops
         * both updating and drawing. Re-enabling restores it, provided every
         * ancestor is active. Detaching a node from a disabled parent (via
         * @ref remove or the parent's destruction) likewise restores it: a root
         * is effectively active whenever its own flag is.
         *
         * Not callable during a traversal — see the class notes.
         */
        void set_active(bool active);

        /**
         * @brief Attaches @p child below this node.
         *
         * Re-parents @p child (detaching it from any previous parent first),
         * points its transform at this node so world matrices propagate, and
         * hands it this node's store: a child with no store simply adopts it,
         * while a child already scoped to a *different* store has its subtree's
         * components migrated into this one (see @ref set_store). A parent
         * without a store leaves the child's store untouched.
         *
         * Rejected, with an error logged and no change made, when @p child is
         * this node or one of its ancestors — that would close a cycle.
         *
         * Not callable during a traversal — see the class notes.
         */
        void add(node& child);

        /**
         * @brief Detaches @p child, returning it to world space. No-op if not a child.
         *
         * The detached child becomes a root, so its effective-active state
         * reverts to its own flag (components hidden only because an ancestor
         * was disabled are shown again).
         *
         * Not callable during a traversal — see the class notes.
         */
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
         * Skipped entirely — this node and its subtree — unless the node is
         * effectively active. Driven once per frame from
         * @ref runtime::context::update on the scene root, after game-module
         * @c on_frame has moved nodes and before the renderer walks the frame.
         */
        void update_subtree();

        /**
         * @brief World-space matrix of this node.
         *
         * Equivalent to @c transform.get_world_matrix(): @c parent.world * local
         * when parented, otherwise the local matrix.
         */
        core::math::mat4 world_matrix() const;

        /**
         * @brief Sets the component store this node — and its whole subtree —
         *        draws its component pools from.
         *
         * Usually called indirectly: a scene's root is given the store by
         * @ref runtime::context, and @ref add hands it down the tree. A node
         * that already carries components has them migrated into the new
         * store (moved, so no @c on_destroy / @c on_attach fires and every
         * external registration survives). Passing @c nullptr unscopes the
         * subtree; components cannot exist without a pool, so any it carries
         * are freed (with @c on_destroy) first.
         */
        void set_store(component_store* store);

        /** @brief Component store backing this node, or @c nullptr if unscoped. */
        component_store* store() const noexcept;

        /**
         * @brief The scene this node belongs to, or @c nullptr when its store
         *        is not owned by a @ref runtime::context (or it has none).
         *
         * This is how a component reaches the scene's deferred command queue
         * from inside a hook: @c owner.scene()->defer_destroy(owner, ...).
         */
        context* scene() const noexcept;

        /**
         * @brief True between a @c context::defer_destroy(*this) call and the
         *        end of the @c context::update that applies it.
         *
         * Lets a component's @c on_update skip work on a node that is already
         * on its way out.
         */
        bool is_destroy_pending() const noexcept;

        /**
         * @brief Adds (or replaces) the @c C component on this node.
         *
         * Stores @p value in the scene's pool for @c C and records its handle.
         * Replaces any existing @c C on this node. Returns a pointer to the
         * pooled component, or @c nullptr if the node has no store yet — or if
         * called during a traversal, in which case (release builds) the add is
         * applied at the end of the current @c context::update instead.
         */
        template<typename C>
        C* add_component(C value)
        {
            if (m_store == nullptr)
            {
                LOG_WRN("runtime::node::add_component: node has no component store");
                return nullptr;
            }

            if (reject_during_traversal("add_component"))
            {
                // Release builds apply the add once the traversal has unwound.
                // The value is boxed so the command stays copyable (as
                // std::function requires) even for move-only components.
                auto boxed = std::make_shared<C>(std::move(value));
                defer([this, boxed] { add_component<C>(std::move(*boxed)); });
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

        /**
         * @brief Removes this node's @c C component and frees its pooled
         *        storage (dispatching @c on_destroy). No-op if absent.
         *
         * Not callable during a traversal — see the class notes.
         */
        template<typename C>
        void remove_component()
        {
            if (!has_component<C>())
            {
                return;
            }
            if (reject_during_traversal("remove_component"))
            {
                defer([this] { remove_component<C>(); });
                return;
            }

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

        /**
         * @brief Removes every component on this node, freeing their pooled
         *        storage (dispatching @c on_destroy on each). Children are
         *        untouched.
         *
         * Not callable during a traversal — see the class notes.
         */
        void remove_all_components();

    private:
        // The scene applies deferred commands against these.
        friend struct context;

        struct component_entry
        {
            std::type_index type;
            component_handle handle;
        };

        // Recomputes effective-active from @p parent_effective, dispatching
        // on_active_changed to this node's components when it flips, and recurses
        // into children with the new value.
        void refresh_active(bool parent_effective);

        // Frees this node's components without any traversal check; shared by
        // the destructor, remove_all_components and set_store(nullptr).
        void release_components();

        // Unlinks this node from its parent (child list and transform) without
        // touching the active state; add and remove decide what follows.
        void detach_from_parent();

        // True when the owning scene is mid-traversal, in which case the
        // named immediate mutation must not run. Logs an error, asserts in
        // debug builds, and in release builds tells the caller to defer.
        bool reject_during_traversal(const char* operation) const;

        // Queues @p command on the owning scene for the end of its update.
        void defer(std::function<void()> command);

        node* m_parent;
        std::vector<node*> m_children;
        component_store* m_store;
        std::vector<component_entry> m_components;

        bool m_active;
        bool m_effective_active;
        bool m_destroy_pending;
    };
} // namespace runtime
