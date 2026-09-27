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
 * @file component.hpp
 * @brief Component handles and the per-scene store that pools component data.
 *
 * A @ref node (the entity) owns no component data directly: it holds
 * @ref component_handle values that index pools owned by the scene's
 * @ref component_store. This is the "subsystem pools the storage, the entity
 * holds a handle" model — one @ref core::pool per component type,
 * keyed by @c std::type_index. Component types are plain structs; any default
 * + copy/move-constructible struct can be used as a component without
 * registering it anywhere first.
 *
 * Each pool also records which node owns every live component, so the scene
 * can walk one type's pool as a unit — dispatching @c on_update per type
 * (@ref runtime::scene::update) or answering "every @c mesh_component"
 * (@ref runtime::scene::each / @ref runtime::scene::view) — rather than
 * chasing the node tree.
 */

#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#include <core/log.hpp>
#include <core/pool.hpp>

namespace runtime
{
    struct node;
    struct scene;

    template<typename C, typename... Rest>
    struct component_view;

    /**
     * @brief Untyped handle into a @ref component_store pool.
     *
     * Mirrors @ref core::pool_handle but drops the type tag so a
     * @ref node can hold handles to components of different types in one list;
     * the store re-types it on access using the component's @c std::type_index.
     * A default-constructed handle (@c generation == 0) is invalid.
     */
    struct component_handle
    {
        uint32_t index{0};
        uint32_t generation{0};

        bool valid() const noexcept
        {
            return generation != 0;
        }
    };

    /**
     * @brief Where a node stood in the most recent scene update walk.
     *
     * Written by @ref runtime::scene::update as it walks the tree for
     * transform propagation: @c stamp names the update that reached the node
     * (it is only reached while effectively active and linked under the scene
     * root) and @c order is its depth-first position in that walk. The store
     * reads it to dispatch @c on_update per component type, to owners the walk
     * reached, parents before children. Lives in the node; the store keeps a
     * pointer to it beside each component's owner.
     */
    struct visit_mark
    {
        uint64_t stamp{0};
        uint32_t order{0};
    };

    /**
     * @brief Owns one @ref core::pool per component type for a scene.
     *
     * Created and owned by @ref runtime::scene; @ref node funnels its
     * @c add_component / @c get_component / @c remove_component calls through
     * here. Type-erased so the store needs no compile-time list of component
     * types — a pool is created lazily the first time a given type is added,
     * and the order in which types first appear is the order the per-type
     * @c on_update dispatch runs them in.
     *
     * Destroying the store dispatches @c on_destroy to every component still
     * alive in it (in pool slot order) before their values are destroyed, so a
     * component whose node outlives the scene still unwinds its external
     * registrations. Not copyable or movable: nodes hold the store's address.
     */
    struct component_store
    {
        component_store() = default;

        component_store(const component_store&) = delete;
        component_store& operator=(const component_store&) = delete;
        component_store(component_store&&) = delete;
        component_store& operator=(component_store&&) = delete;

        /**
         * @brief The scene this store belongs to, or @c nullptr for a
         *        standalone store (one built outside a @ref runtime::scene,
         *        e.g. by a test).
         *
         * Nodes reach their scene — and its deferred command queue — through
         * their store, so a component's @c on_update can call
         * @c owner.scene()->destroy_node(owner).
         */
        runtime::scene* scene() const noexcept
        {
            return m_scene;
        }

        /**
         * @brief Stores @p value in the pool for @c C and returns its handle.
         *
         * The component has no owning node, so the per-type @c on_update
         * dispatch and @ref each skip it; @ref node::add_component is the way
         * to give a node a component.
         */
        template<typename C>
        component_handle add(C value)
        {
            return insert<C>(std::move(value), owner_record{});
        }

        /** @brief Pointer to the @c C named by @p handle, or @c nullptr if absent/stale. */
        template<typename C>
        C* get(component_handle handle) noexcept
        {
            typed_pool<C>* typed = find_pool<C>();
            return typed != nullptr ? typed->data.get(make_handle<C>(handle)) : nullptr;
        }

        /** @brief Frees the @c C named by @p handle. No-op if absent/stale. */
        template<typename C>
        void remove(component_handle handle) noexcept
        {
            erase(std::type_index(typeid(C)), handle);
        }

        /** @brief Number of live @c C components in this store. */
        template<typename C>
        std::size_t count() const noexcept
        {
            auto it = m_pools.find(std::type_index(typeid(C)));
            return it != m_pools.end() ? static_cast<const typed_pool<C>&>(*it->second).data.size() : 0;
        }

        /**
         * @brief Invokes @p fn on every live @c C, in pool slot order.
         *
         * @p fn takes either @c (node&, C&) — called for each component a
         * node owns — or @c (C&) — called for every component, owned or
         * not. Slot order is neither insertion nor hierarchy order. @p fn must
         * not add a @c C (that may grow the pool under the walk); removing
         * one is fine. @ref runtime::scene::each wraps this in a traversal
         * so the node APIs enforce that.
         */
        template<typename C, typename Fn>
        void each(Fn&& fn)
        {
            typed_pool<C>* typed = find_pool<C>();
            if (typed == nullptr)
            {
                return;
            }
            for (auto it = typed->data.begin(); it != typed->data.end(); ++it)
            {
                if constexpr (std::is_invocable_v<Fn&, node&, C&>)
                {
                    if (node* owner = typed->owner_at(it.handle().index).owner)
                    {
                        fn(*owner, *it);
                    }
                }
                else
                {
                    fn(*it);
                }
            }
        }

        /**
         * @brief Type-erased erase used by @ref node teardown.
         *
         * Lets a node free its components without naming each type — it only
         * keeps the @c std::type_index recorded when the component was added.
         * Dispatches the component's @c on_destroy first, if it defines one.
         */
        void erase(std::type_index type, component_handle handle) noexcept
        {
            auto it = m_pools.find(type);
            if (it != m_pools.end())
            {
                it->second->erase(handle);
            }
        }

        /**
         * @brief Moves the component named by @p type / @p handle into
         *        @p target's pool for the same type and returns its new handle.
         *
         * Used by @ref node::set_store when a populated node is re-parented
         * into another scene. The value is moved, not destroyed, so neither
         * @c on_destroy nor @c on_attach fires: the component keeps whatever
         * external registrations it made (they key off the node's transform
         * and the heap objects the component owns, both of which survive the
         * move). Returns an invalid handle if @p handle is stale; returns
         * @p handle unchanged when @p target is this store.
         */
        component_handle migrate(std::type_index type, component_handle handle, component_store& target)
        {
            if (&target == this)
            {
                return handle;
            }
            auto it = m_pools.find(type);
            if (it == m_pools.end())
            {
                return component_handle{};
            }
            return it->second->migrate(handle, target);
        }

        /**
         * @brief Type-erased per-node update used by @ref node::update_subtree.
         *
         * Dispatches to the component's @c on_update(node&) if it defines one.
         * The scene's own per-frame update does not come through here — it
         * runs each type's pool as a unit via @ref update_visited.
         */
        void update(std::type_index type, component_handle handle, node& owner) noexcept
        {
            auto it = m_pools.find(type);
            if (it != m_pools.end())
            {
                it->second->update(handle, owner);
            }
        }

        /**
         * @brief Per-type @c on_update dispatch for one scene update.
         *
         * For each component type that defines @c on_update — in the order
         * the types first appeared in this store — walks that type's pool and
         * calls @c on_update on every component whose owner the update walk
         * stamped with @p stamp (see @ref visit_mark), owners ordered as the
         * walk met them: parents before children, siblings in insertion
         * order. Types without the hook cost nothing.
         */
        void update_visited(uint64_t stamp)
        {
            // Indexed rather than range-for: a new type cannot appear during a
            // traversal (add_component defers), but the loop stays correct if
            // one ever does.
            for (std::size_t i = 0; i < m_pool_order.size(); ++i)
            {
                m_pool_order[i]->update_visited(stamp);
            }
        }

        /**
         * @brief Type-erased active/visible toggle used by @ref node::set_active.
         *
         * Dispatches to the component's @c on_active_changed(node&, bool) if it
         * defines one, so components that own external state (a renderable's
         * registration) can show or hide it when a subtree is enabled/disabled.
         */
        void set_active(std::type_index type, component_handle handle, node& owner, bool active) noexcept
        {
            auto it = m_pools.find(type);
            if (it != m_pools.end())
            {
                it->second->set_active(handle, owner, active);
            }
        }

    private:
        // The owning scene installs itself here on construction; nodes insert
        // with an owner and clone through the private entry points below.
        friend struct scene;
        friend struct node;
        template<typename C, typename... Rest>
        friend struct component_view;

        // Who owns a pooled component: the node, and where that node's
        // update-walk mark lives. Both null for a component added straight
        // to the store (@ref add).
        struct owner_record
        {
            node* owner{nullptr};
            const visit_mark* visit{nullptr};
        };

        struct pool_base
        {
            virtual ~pool_base() = default;
            virtual void erase(component_handle handle) noexcept = 0;
            virtual component_handle migrate(component_handle handle, component_store& target) = 0;
            virtual component_handle
            clone_into(component_handle handle, component_store& target, owner_record owner) = 0;
            virtual void attach(component_handle handle, node& owner) = 0;
            virtual void update(component_handle handle, node& owner) noexcept = 0;
            virtual void update_visited(uint64_t stamp) = 0;
            virtual void set_active(component_handle handle, node& owner, bool active) noexcept = 0;
        };

        template<typename C>
        struct typed_pool final : pool_base
        {
            core::pool<C> data;

            // Parallel to data's slots, indexed by slot index.
            std::vector<owner_record> owners;

            // Scratch for update_visited, kept to reuse its capacity.
            struct ordered_handle
            {
                uint32_t order;
                typename core::pool<C>::handle handle;
            };
            std::vector<ordered_handle> visit_order;

            // The store is going away with components still alive in it (a
            // node that outlives its scene, or the scene itself tearing down
            // with nodes attached). Give each one its on_destroy before the
            // pool destroys the values, exactly as erase() would have; the
            // pool member is destroyed after this body runs.
            ~typed_pool() override
            {
                if constexpr (requires(C& c) { c.on_destroy(); })
                {
                    data.for_each([](C& c) { c.on_destroy(); });
                }
            }

            owner_record owner_at(uint32_t index) const noexcept
            {
                return index < owners.size() ? owners[index] : owner_record{};
            }

            void set_owner(uint32_t index, owner_record record)
            {
                if (index >= owners.size())
                {
                    owners.resize(static_cast<std::size_t>(index) + 1);
                }
                owners[index] = record;
            }

            void erase(component_handle handle) noexcept override
            {
                auto h = make_handle<C>(handle);
                // Give components that manage external state (e.g. a renderer
                // registration) a chance to unwind it before the slot — and the
                // data it owns — is freed. Plain-data components define no
                // on_destroy and skip this entirely.
                if constexpr (requires(C& c) { c.on_destroy(); })
                {
                    if (C* c = data.get(h))
                    {
                        c->on_destroy();
                    }
                }
                if (data.contains(h))
                {
                    data.erase(h);
                    owners[h.index] = owner_record{};
                }
            }

            component_handle migrate(component_handle handle, component_store& target) override
            {
                auto h = make_handle<C>(handle);
                C* c = data.get(h);
                if (c == nullptr)
                {
                    return component_handle{};
                }
                // Move the value across first so a throwing insert leaves the
                // source intact; the slot here is then freed without any
                // on_destroy — the component lives on in the target pool.
                component_handle moved = target.insert<C>(std::move(*c), owners[h.index]);
                data.erase(h);
                owners[h.index] = owner_record{};
                return moved;
            }

            // Copies the component into @p target for @p owner, through the
            // type's clone() when it has one, else its copy constructor. A
            // clone() returning std::optional<C> may decline with an empty
            // one (explaining why itself), which leaves the component off the
            // copy. The copy is made before the insert, which may grow this
            // very pool. on_attach is left to the caller, once the node has
            // recorded the handle (the same order add_component uses).
            component_handle clone_into(component_handle handle, component_store& target, owner_record owner) override
            {
                const C* source = data.get(make_handle<C>(handle));
                if (source == nullptr)
                {
                    return component_handle{};
                }
                if constexpr (requires(const C& c) {
                                  { c.clone() } -> std::convertible_to<C>;
                              })
                {
                    C copy = source->clone();
                    return target.insert<C>(std::move(copy), owner);
                }
                else if constexpr (requires(const C& c) {
                                       { c.clone() } -> std::same_as<std::optional<C>>;
                                   })
                {
                    std::optional<C> copy = source->clone();
                    if (!copy.has_value())
                    {
                        return component_handle{};
                    }
                    return target.insert<C>(std::move(*copy), owner);
                }
                else if constexpr (std::is_copy_constructible_v<C>)
                {
                    C copy = *source;
                    return target.insert<C>(std::move(copy), owner);
                }
                else
                {
                    (void)target;
                    (void)owner;
                    LOG_WRN("runtime::component_store: component type '%s' has no clone() and is not copyable; "
                            "left off the clone",
                            typeid(C).name());
                    return component_handle{};
                }
            }

            void attach(component_handle handle, node& owner) override
            {
                if constexpr (requires(C& c, node& n) { c.on_attach(n); })
                {
                    if (C* c = data.get(make_handle<C>(handle)))
                    {
                        c->on_attach(owner);
                    }
                }
            }

            void update(component_handle handle, node& owner) noexcept override
            {
                if constexpr (requires(C& c, node& n) { c.on_update(n); })
                {
                    if (C* c = data.get(make_handle<C>(handle)))
                    {
                        c->on_update(owner);
                    }
                }
            }

            void update_visited(uint64_t stamp) override
            {
                if constexpr (requires(C& c, node& n) { c.on_update(n); })
                {
                    // Borrow the scratch list (a hook that re-enters the
                    // update finds it empty rather than reshuffled under us).
                    std::vector<ordered_handle> pending;
                    pending.swap(visit_order);
                    pending.clear();

                    // One contiguous pass over the pool picks out the
                    // components whose owner this update reached...
                    for (auto it = data.begin(); it != data.end(); ++it)
                    {
                        const owner_record record = owner_at(it.handle().index);
                        if (record.visit != nullptr && record.visit->stamp == stamp)
                        {
                            pending.push_back(ordered_handle{record.visit->order, it.handle()});
                        }
                    }

                    // ...and they run in walk order, parents before children.
                    // Slot order usually follows creation order, which usually
                    // follows the tree, so the sort is mostly skipped.
                    auto by_order = [](const ordered_handle& a, const ordered_handle& b) { return a.order < b.order; };
                    if (!std::is_sorted(pending.begin(), pending.end(), by_order))
                    {
                        std::sort(pending.begin(), pending.end(), by_order);
                    }

                    for (const ordered_handle& entry : pending)
                    {
                        // Re-resolved: an earlier hook may have freed it (a
                        // node destroyed mid-walk in a release build).
                        if (C* c = data.get(entry.handle))
                        {
                            c->on_update(*owners[entry.handle.index].owner);
                        }
                    }
                    visit_order.swap(pending);
                }
                else
                {
                    (void)stamp;
                }
            }

            void set_active(component_handle handle, node& owner, bool active) noexcept override
            {
                if constexpr (requires(C& c, node& n, bool a) { c.on_active_changed(n, a); })
                {
                    if (C* c = data.get(make_handle<C>(handle)))
                    {
                        c->on_active_changed(owner, active);
                    }
                }
            }
        };

        template<typename C>
        static typename core::pool<C>::handle make_handle(component_handle handle) noexcept
        {
            return typename core::pool<C>::handle{handle.index, handle.generation};
        }

        template<typename C>
        component_handle insert(C value, owner_record owner)
        {
            typed_pool<C>& typed = pool_for<C>();
            auto h = typed.data.insert(std::move(value));
            typed.set_owner(h.index, owner);
            return component_handle{h.index, h.generation};
        }

        // Type-erased clone / attach used by node's clone path.
        component_handle
        clone_into(std::type_index type, component_handle handle, component_store& target, owner_record owner)
        {
            auto it = m_pools.find(type);
            return it != m_pools.end() ? it->second->clone_into(handle, target, owner) : component_handle{};
        }

        void attach(std::type_index type, component_handle handle, node& owner)
        {
            auto it = m_pools.find(type);
            if (it != m_pools.end())
            {
                it->second->attach(handle, owner);
            }
        }

        template<typename C>
        typed_pool<C>* find_pool() noexcept
        {
            auto it = m_pools.find(std::type_index(typeid(C)));
            return it != m_pools.end() ? static_cast<typed_pool<C>*>(it->second.get()) : nullptr;
        }

        template<typename C>
        typed_pool<C>& pool_for()
        {
            std::type_index key{typeid(C)};
            auto it = m_pools.find(key);
            if (it == m_pools.end())
            {
                it = m_pools.emplace(key, std::make_unique<typed_pool<C>>()).first;
                m_pool_order.push_back(it->second.get());
            }
            return static_cast<typed_pool<C>&>(*it->second);
        }

        std::unordered_map<std::type_index, std::unique_ptr<pool_base>> m_pools;
        // The same pools in the order their types first appeared: the
        // per-type update order.
        std::vector<pool_base*> m_pool_order;
        runtime::scene* m_scene{nullptr};
    };
} // namespace runtime
