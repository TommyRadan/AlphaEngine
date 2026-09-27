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
 * @file behavior_component.hpp
 * @brief Component that attaches a @ref runtime::behavior to a node.
 */

#pragma once

#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include <core/subscription.hpp>
#include <runtime/behavior.hpp>
#include <runtime/node.hpp>

namespace runtime
{
    /**
     * @brief Gives a node a @ref behavior: the adapter between the
     *        polymorphic behaviour and the scene's component store.
     *
     * Owns the behaviour on the heap, so its address — and @c this inside its
     * hooks — stays put while the component is relocated within its pool, and
     * forwards the hooks the store dispatches:
     *
     * - @ref on_attach records the owner, subscribes the behaviour to the
     *   fixed step (@c core::frame on the engine's event bus) and, on an
     *   effectively active node, enables it;
     * - @ref on_update — the scene's per-type update, once per rendered
     *   frame — runs @ref behavior::on_update with the engine clock's frame
     *   delta;
     * - @ref on_active_changed enables or disables it with its node;
     * - @ref on_destroy disables it if needed, runs
     *   @ref behavior::on_destroy, drops the fixed-step subscription and
     *   deletes the behaviour.
     *
     * The fixed step runs with the behaviour's scene marked as traversing,
     * like the scene's own update, so the two update hooks follow the same
     * rule for structural changes (see behavior.hpp). @ref behavior::on_start
     * runs before whichever update reaches the behaviour first.
     *
     * Attaching one needs a live engine (for the event bus). Move-only;
     * cloned through @ref clone, which defers to @ref behavior::clone.
     */
    struct behavior_component
    {
        /** @brief Empty component — carries no behaviour. */
        behavior_component() = default;

        /** @brief Takes ownership of @p logic, which is not attached anywhere yet. */
        explicit behavior_component(std::unique_ptr<behavior> logic) noexcept;

        behavior_component(behavior_component&&) noexcept = default;
        behavior_component& operator=(behavior_component&&) noexcept = default;
        behavior_component(const behavior_component&) = delete;
        behavior_component& operator=(const behavior_component&) = delete;
        ~behavior_component() = default;

        /** @brief Attaches the behaviour to @p owner (see the class notes). */
        void on_attach(node& owner);

        /** @brief Runs the behaviour's frame update (and its start, the first time). */
        void on_update(node& owner);

        /** @brief Enables or disables the behaviour as @p owner becomes (in)active. */
        void on_active_changed(node& owner, bool active);

        /** @brief Disables and destroys the behaviour. */
        void on_destroy();

        /**
         * @brief A component carrying @ref behavior::clone of this one's
         *        behaviour, for @c scene::clone.
         *
         * Empty — so the store leaves the component off the copy — when the
         * behaviour's type does not implement @ref behavior::clone, with a
         * warning naming it.
         */
        std::optional<behavior_component> clone() const;

        /** @brief The behaviour, or @c nullptr for an empty component. */
        behavior* get() const noexcept
        {
            return m_behavior.get();
        }

        /** @brief The behaviour as a @c B, or @c nullptr if it is not one (or there is none). */
        template<typename B>
        B* get_as() const noexcept
        {
            return dynamic_cast<B*>(m_behavior.get());
        }

    private:
        // The lifecycle state transitions (behavior keeps that state private
        // and befriends this adapter).
        static void enable(behavior& logic);
        static void disable(behavior& logic);
        // True when @p logic is to be updated now — enabled, attached, its
        // node not queued for destruction — starting it the first time.
        static bool begin_update(behavior& logic);
        static void fixed_step(behavior& logic, float delta_time);

        std::unique_ptr<behavior> m_behavior;
        // The fixed-step listener; its callback holds the behaviour's heap
        // address, never this component's.
        core::subscription m_fixed_step;
    };

    /**
     * @brief Constructs a @c B from @p args and attaches it to @p target in a
     *        new @ref behavior_component; returns it, or @c nullptr when the
     *        node could not take the component yet (no store, or called
     *        during a traversal — see @c node::add_component).
     *
     * Replaces any behaviour @p target already has.
     */
    template<typename B, typename... Args>
    B* add_behavior(node& target, Args&&... args)
    {
        static_assert(std::is_base_of_v<behavior, B>, "add_behavior: B must derive from runtime::behavior");
        auto logic = std::make_unique<B>(std::forward<Args>(args)...);
        B* created = logic.get();
        behavior_component* component = target.add_component(behavior_component{std::move(logic)});
        return component != nullptr && component->get() == created ? created : nullptr;
    }
} // namespace runtime
