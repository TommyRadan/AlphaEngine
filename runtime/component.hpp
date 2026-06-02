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
 */

#pragma once

#include <memory>
#include <typeindex>
#include <unordered_map>

#include <core/pool.hpp>

namespace runtime
{
    struct node;

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
     * @brief Owns one @ref core::pool per component type for a scene.
     *
     * Created and owned by @ref runtime::context; @ref node funnels its
     * @c add_component / @c get_component / @c remove_component calls through
     * here. Type-erased so the store needs no compile-time list of component
     * types — a pool is created lazily the first time a given type is added.
     */
    struct component_store
    {
        /** @brief Stores @p value in the pool for @c C and returns its handle. */
        template<typename C>
        component_handle add(C value)
        {
            auto h = pool_for<C>().data.insert(std::move(value));
            return component_handle{h.index, h.generation};
        }

        /** @brief Pointer to the @c C named by @p handle, or @c nullptr if absent/stale. */
        template<typename C>
        C* get(component_handle handle) noexcept
        {
            auto it = m_pools.find(std::type_index(typeid(C)));
            if (it == m_pools.end())
            {
                return nullptr;
            }
            auto& typed = static_cast<typed_pool<C>&>(*it->second);
            return typed.data.get(make_handle<C>(handle));
        }

        /** @brief Frees the @c C named by @p handle. No-op if absent/stale. */
        template<typename C>
        void remove(component_handle handle) noexcept
        {
            erase(std::type_index(typeid(C)), handle);
        }

        /**
         * @brief Type-erased erase used by @ref node teardown.
         *
         * Lets a node free its components without naming each type — it only
         * keeps the @c std::type_index recorded when the component was added.
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
         * @brief Type-erased per-frame update used by @ref node traversal.
         *
         * Dispatches to the component's @c on_update(node&) if it defines one,
         * so components that derive state from their node (a light's position,
         * a camera's pose) can refresh it once the world transforms are settled.
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
        struct pool_base
        {
            virtual ~pool_base() = default;
            virtual void erase(component_handle handle) noexcept = 0;
            virtual void update(component_handle handle, node& owner) noexcept = 0;
            virtual void set_active(component_handle handle, node& owner, bool active) noexcept = 0;
        };

        template<typename C>
        struct typed_pool final : pool_base
        {
            core::pool<C> data;

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
                data.erase(h);
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
        typed_pool<C>& pool_for()
        {
            std::type_index key{typeid(C)};
            auto it = m_pools.find(key);
            if (it == m_pools.end())
            {
                it = m_pools.emplace(key, std::make_unique<typed_pool<C>>()).first;
            }
            return static_cast<typed_pool<C>&>(*it->second);
        }

        std::unordered_map<std::type_index, std::unique_ptr<pool_base>> m_pools;
    };
} // namespace runtime
