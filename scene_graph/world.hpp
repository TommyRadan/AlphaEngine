/**
 * Copyright (c) 2015-2025 Tomislav Radanovic
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
 * @file world.hpp
 * @brief Minimal stdlib-only Entity-Component-System container.
 *
 * The @ref scene_graph::world owns a set of @ref entity_id handles and
 * arbitrary user-defined component types associated with them. Component
 * storage is type-erased by @c std::type_index and implemented on top of
 * @c std::unordered_map, so any default-constructible, movable struct can
 * be used as a component with no registration step.
 *
 * The world is not thread-safe - mutate it only from the main-loop thread.
 */

#pragma once

#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <scene_graph/entity.hpp>

namespace scene_graph
{
    /**
     * @brief Entity-Component-System container.
     *
     * Entities are opaque ids (see @ref entity_id). Components are stored in
     * type-erased per-type maps keyed by entity. Lookup and mutation cost
     * one or two @c unordered_map operations - this is intentionally a
     * simple, correct implementation rather than a cache-optimal one.
     *
     * Typical usage:
     * @code
     * scene_graph::world w;
     * auto e = w.create_entity();
     * w.add<transform>(e, transform{});
     * for (auto id : w.view<transform, mesh>())
     * {
     *     auto& t = *w.get<transform>(id);
     *     // ...
     * }
     * @endcode
     */
    class world
    {
    public:
        world() = default;
        ~world() = default;

        world(const world&) = delete;
        world& operator=(const world&) = delete;
        world(world&&) = delete;
        world& operator=(world&&) = delete;

        /**
         * @brief Allocates a fresh entity id.
         *
         * The returned id is never @ref invalid_entity and is unique among
         * currently-live entities in this world. Ids of destroyed entities
         * may eventually be reused; callers that retain ids across
         * @ref destroy_entity calls must guard against that themselves.
         *
         * @return A new live @ref entity_id.
         */
        entity_id create_entity();

        /**
         * @brief Destroys @p entity and erases every component attached to it.
         *
         * No-op if @p entity is @ref invalid_entity or already destroyed.
         *
         * @param entity Entity handle to destroy.
         */
        void destroy_entity(entity_id entity);

        /**
         * @brief Reports whether @p entity currently exists in this world.
         * @param entity Entity handle to query.
         * @return @c true if live, @c false if destroyed or invalid.
         */
        [[nodiscard]] bool is_alive(entity_id entity) const;

        /**
         * @brief Attaches (or overwrites) a component of type @p T on @p entity.
         *
         * The component is stored by move. If @p entity already has a
         * component of type @p T, it is replaced.
         *
         * @tparam T         Component type. Must be move-constructible.
         * @param  entity    Live entity to attach the component to.
         * @param  component Component value, taken by value and moved in.
         * @return Reference to the stored component, valid until the next
         *         mutation of the same component storage.
         */
        template<typename T>
        T& add(entity_id entity, T component);

        /**
         * @brief Looks up the component of type @p T on @p entity.
         *
         * @tparam T      Component type.
         * @param  entity Entity handle.
         * @return Pointer to the stored component, or @c nullptr if
         *         @p entity has no component of type @p T.
         */
        template<typename T>
        [[nodiscard]] T* get(entity_id entity);

        /** @brief Const overload of @ref get. */
        template<typename T>
        [[nodiscard]] const T* get(entity_id entity) const;

        /**
         * @brief Reports whether @p entity has a component of type @p T.
         * @tparam T      Component type.
         * @param  entity Entity handle.
         * @return @c true if the component is present.
         */
        template<typename T>
        [[nodiscard]] bool has(entity_id entity) const;

        /**
         * @brief Removes the component of type @p T from @p entity, if any.
         *
         * @tparam T      Component type.
         * @param  entity Entity handle.
         * @return @c true if a component was removed, @c false if none was
         *         present.
         */
        template<typename T>
        bool remove(entity_id entity);

        /**
         * @brief Returns the ids of all entities that have every component
         *        in @p Ts.
         *
         * The returned vector is a snapshot - mutating the world after the
         * call does not invalidate it, but the set of matching entities may
         * drift from what the snapshot contains. Prefer to consume the
         * result inside a single frame.
         *
         * @tparam Ts Component type pack. With no parameters, returns every
         *            live entity.
         * @return A newly-allocated vector of matching ids.
         */
        template<typename... Ts>
        [[nodiscard]] std::vector<entity_id> view() const;

        /** @brief Number of currently live entities. */
        [[nodiscard]] std::size_t entity_count() const
        {
            return m_alive.size();
        }

        /**
         * @brief Drops every entity and component.
         *
         * After @ref clear, @ref entity_count is 0 and future ids start
         * from 1 again.
         */
        void clear();

    private:
        /**
         * @brief Type-erased interface used to drop a single entity's
         *        component from any per-type storage without knowing @p T.
         *
         * Needed so @ref destroy_entity can uniformly sweep across all
         * registered component types.
         */
        struct component_storage_base
        {
            virtual ~component_storage_base() = default;
            virtual void erase(entity_id entity) = 0;
        };

        /**
         * @brief Concrete per-component-type storage: a map from entity to
         *        component value.
         *
         * Instantiated lazily the first time @ref add is called with a
         * given @p T.
         */
        template<typename T>
        struct component_storage : component_storage_base
        {
            std::unordered_map<entity_id, T> data;

            void erase(entity_id entity) override
            {
                data.erase(entity);
            }
        };

        template<typename T>
        component_storage<T>* get_storage();

        template<typename T>
        const component_storage<T>* get_storage() const;

        entity_id m_next_id{1};
        std::unordered_set<entity_id> m_alive;
        std::unordered_map<std::type_index, std::unique_ptr<component_storage_base>> m_components;
    };

    // ---- template implementations ----

    template<typename T>
    T& world::add(entity_id entity, T component)
    {
        auto* storage = get_storage<T>();
        auto [it, inserted] = storage->data.insert_or_assign(entity, std::move(component));
        return it->second;
    }

    template<typename T>
    T* world::get(entity_id entity)
    {
        auto* storage = get_storage<T>();
        auto it = storage->data.find(entity);
        if (it == storage->data.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    template<typename T>
    const T* world::get(entity_id entity) const
    {
        const auto* storage = get_storage<T>();
        if (storage == nullptr)
        {
            return nullptr;
        }
        auto it = storage->data.find(entity);
        if (it == storage->data.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    template<typename T>
    bool world::has(entity_id entity) const
    {
        const auto* storage = get_storage<T>();
        if (storage == nullptr)
        {
            return false;
        }
        return storage->data.find(entity) != storage->data.end();
    }

    template<typename T>
    bool world::remove(entity_id entity)
    {
        auto it = m_components.find(std::type_index(typeid(T)));
        if (it == m_components.end())
        {
            return false;
        }
        auto* storage = static_cast<component_storage<T>*>(it->second.get());
        return storage->data.erase(entity) > 0;
    }

    template<typename... Ts>
    std::vector<entity_id> world::view() const
    {
        std::vector<entity_id> result;
        if constexpr (sizeof...(Ts) == 0)
        {
            result.reserve(m_alive.size());
            for (auto id : m_alive)
            {
                result.push_back(id);
            }
            return result;
        }
        else
        {
            // If any required component type has never been stored, the view
            // is empty by definition - short-circuit instead of paying for
            // an iteration.
            const bool any_missing = (... || (get_storage<Ts>() == nullptr));
            if (any_missing)
            {
                return result;
            }

            for (auto id : m_alive)
            {
                if ((... && has<Ts>(id)))
                {
                    result.push_back(id);
                }
            }
            return result;
        }
    }

    template<typename T>
    world::component_storage<T>* world::get_storage()
    {
        const auto key = std::type_index(typeid(T));
        auto it = m_components.find(key);
        if (it == m_components.end())
        {
            auto owned = std::make_unique<component_storage<T>>();
            auto* raw = owned.get();
            m_components.emplace(key, std::move(owned));
            return raw;
        }
        return static_cast<component_storage<T>*>(it->second.get());
    }

    template<typename T>
    const world::component_storage<T>* world::get_storage() const
    {
        const auto key = std::type_index(typeid(T));
        auto it = m_components.find(key);
        if (it == m_components.end())
        {
            return nullptr;
        }
        return static_cast<const component_storage<T>*>(it->second.get());
    }

    /**
     * @brief Built-in component that hooks an entity into the scene render pass.
     *
     * When the @ref context is active, every entity that carries a
     * @ref renderable_component has its @ref draw callback invoked during
     * the @c render_scene event broadcast. This demonstrates how gameplay
     * code can register visuals through the ECS instead of subscribing to
     * @c render_scene directly from a game module.
     */
    struct renderable_component
    {
        /** @brief Called once per scene render. Must not be empty at dispatch time. */
        std::function<void()> draw;
    };
} // namespace scene_graph
