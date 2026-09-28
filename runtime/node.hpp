// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file node.hpp
 * @brief Scene-graph node — the entity in the entity/component model.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <vector>

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <core/math/transform.hpp>
#include <core/string_id.hpp>
#include <runtime/component.hpp>

namespace runtime
{
    struct scene;

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
     * **Ownership.** Nodes are normally owned by a scene: create them with
     * @c scene::create_node, retire them with @c scene::destroy_node, and
     * the scene keeps each one at a stable address until then (and frees
     * whatever is left when it quits). A node can still be constructed
     * directly — a stack node in a test, an embedded root — in which case
     * the caller owns it and must keep it alive while it is wired into a
     * tree. Links are non-owning raw pointers either way: a node never
     * deletes its parent or children. The destructor detaches from the
     * parent, orphans children back to world space, and frees this node's
     * components. Main-thread-only.
     *
     * **Structural mutation during a traversal.** While the scene is walking
     * the tree — inside a component's @c on_update (from the scene's update
     * or @ref update_subtree) or @c on_active_changed (from @ref set_active)
     * — the node, child and component lists being iterated must not change.
     * The immediate APIs (@ref add, @ref remove, @ref set_active,
     * @ref add_component, @ref remove_component, @ref remove_all_components)
     * detect that case through the owning @ref runtime::scene: in debug
     * builds they assert; in release builds they log an error and apply the
     * call at the end of @ref runtime::scene::update instead. Code that
     * needs to mutate the tree from a hook should say so explicitly with the
     * scene's @c destroy_node / @c defer_remove_component / @c defer_reparent
     * / @c defer_set_active, reached via @ref scene.
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
        core::transform transform;

        /** @brief Optional label, used by @ref find. Not required to be unique. */
        const core::string_id& name() const noexcept;

        /**
         * @brief Renames the node, keeping its scene's name index (see
         *        @c scene::find) in step.
         */
        void set_name(core::string_id name);

        /**
         * @brief Returns the first node in this subtree (this node included)
         *        whose @ref name equals @p target, or @c nullptr.
         *
         * Depth-first, in child insertion order; each step is an integer
         * compare of interned ids. For a scene-wide lookup by name without the
         * walk, use @c scene::find.
         */
        node* find(core::string_id target);

        /** @brief This node's world-space position (translation of @ref world_matrix). */
        core::math::vec3 world_position() const;

        /**
         * @brief Places the node at @p world_position in world space by solving
         *        for the local position under the current parent.
         */
        void set_world_position(const core::math::vec3& world_position);

        /**
         * @brief Orients the node so its forward (+X) axis points at @p target
         *        in world space, with @p up (the engine's +Z by default) as the
         *        reference up. Exact when ancestors are unrotated/unscaled;
         *        under a rotated parent the @p up handling is approximate.
         */
        void look_at(const core::math::vec3& target, const core::math::vec3& up = core::math::world_up);

        /** @brief This node's own active flag (ignores ancestors). */
        bool is_active() const noexcept;

        /** @brief True when this node and every ancestor are active. */
        bool is_effective_active() const noexcept;

        /**
         * @brief Enables or disables this node (and, by inheritance, its subtree).
         *
         * A disabled subtree is skipped by @ref update_subtree and its components
         * are told to hide via @c on_active_changed (a @c mesh_component or
         * @c renderable_component destroys its mesh proxy, a
         * @c light_component takes its light out of the enabled lights, a
         * @c camera_component disables its camera), so it stops both updating
         * and drawing. Re-enabling restores it, provided every
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
         * Calls each component's @c on_update(node&) (those that define one),
         * node by node, depth-first. Skipped entirely — this node and its
         * subtree — unless the node is effectively active. The scene's own
         * per-frame @ref runtime::scene::update does not come through here
         * (it dispatches each component type's pool as a unit); this is for
         * driving a subtree by hand, e.g. one that is not linked under a
         * scene root.
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
         * @ref runtime::scene, and @ref add hands it down the tree. A node
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
         *        is not owned by a @ref runtime::scene (or it has none).
         *
         * This is how a component reaches the scene's deferred command queue
         * from inside a hook: @c owner.scene()->destroy_node(owner).
         */
        runtime::scene* scene() const noexcept;

        /**
         * @brief The scene whose node pool holds this node's memory, or
         *        @c nullptr for a caller-owned node.
         *
         * Set by @c scene::create_node. It stays the creating scene even if
         * the node is later re-parented into another scene's tree (that
         * changes @ref scene, not the owner); @c scene::destroy_node routes
         * to it.
         */
        runtime::scene* owning_scene() const noexcept;

        /**
         * @brief True between a @c scene::destroy_node / @c defer_destroy
         *        request for this node and the end of the
         *        @c scene::update that applies it.
         *
         * Lets a component's @c on_update skip work on a node that is already
         * on its way out.
         */
        bool is_destroy_pending() const noexcept;

        /**
         * @brief A shared cell that holds this node's address while the node
         *        lives and is cleared when it is destroyed.
         *
         * For code that keeps a reference to a node it does not own and
         * cannot tell when the node goes away — a script's node handle, say.
         * It keeps the cell and reads it on every use, so a destroyed node
         * reads as @c nullptr instead of a dangling pointer. Every call
         * returns the same cell, so two holders refer to the same node
         * exactly when their cells are the same object. It is cleared once
         * the node's components have been freed (their @c on_destroy hooks
         * still see it set). Made on the first request; a node nobody asks
         * about carries an empty pointer.
         */
        std::shared_ptr<node* const> lifetime_cell();

        /**
         * @brief Adds (or replaces) the @c C component on this node.
         *
         * Stores @p value in the scene's pool for @c C and records its handle.
         * Replaces any existing @c C on this node. Returns a pointer to the
         * pooled component, or @c nullptr if the node has no store yet — or if
         * called during a traversal, in which case (release builds) the add is
         * applied at the end of the current @c scene::update instead.
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
            component_handle handle =
                m_store->insert<C>(std::move(value), component_store::owner_record{this, &m_visit});
            m_components.push_back(component_entry{std::type_index(typeid(C)), handle});

            C* component = m_store->get<C>(handle);
            // Components that bridge to a subsystem (e.g. creating a render
            // proxy) wire themselves up here, now that they know their
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

        /**
         * @brief The type of every component on this node, in the order they
         *        were added (a replaced component counts as added last).
         *
         * For code that walks a node's components without naming their
         * types, such as the scene serializer, which maps each one to its
         * registered name (runtime/reflection.hpp).
         */
        std::vector<std::type_index> component_types() const;

    private:
        // The scene applies deferred commands against these and owns the
        // pool slot, name and visit bookkeeping.
        friend struct scene;

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

        // Gives this (fresh) node a copy of every component on @p source that
        // its type can copy, dispatching on_attach as add_component would.
        void copy_components_from(node& source);

        // Moves this node in or out of its scene's name index; called around
        // every change of name or of scene.
        void index_name();
        void unindex_name();

        node* m_parent;
        std::vector<node*> m_children;
        component_store* m_store;
        std::vector<component_entry> m_components;
        core::string_id m_name;

        // Scene-pool ownership (see owning_scene); m_pool_slot is meaningful
        // only while m_owning_scene is set.
        runtime::scene* m_owning_scene;
        uint32_t m_pool_slot;

        // Written by the scene's update walk; read by the store's per-type
        // on_update dispatch through the owner record.
        visit_mark m_visit;

        // See lifetime_cell; null until first requested.
        std::shared_ptr<node*> m_lifetime;

        bool m_active;
        bool m_effective_active;
        bool m_destroy_pending;
    };
} // namespace runtime
