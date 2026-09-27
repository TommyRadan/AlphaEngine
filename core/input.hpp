// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file input.hpp
 * @brief Maps keyboard, mouse and gamepad input to named actions and axes.
 */

#pragma once

#include <array>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <core/event.hpp>
#include <core/math/math.hpp>
#include <core/subscription.hpp>

namespace core
{
    struct event_bus;
    struct input_settings;

    /** @brief Which physical control an @ref action_binding reads. */
    enum class action_source
    {
        key,
        mouse_button,
        gamepad_button,
        gamepad_axis, /**< A gamepad axis read as a digital trigger past @ref action_binding::threshold. */
    };

    /** @brief Which side of a gamepad axis' range @ref action_source::gamepad_axis treats as "pressed". */
    enum class axis_sign
    {
        positive,
        negative,
    };

    /**
     * @brief One physical control an action can fire from.
     *
     * An action is down while any one of its bindings is active (logical OR). Build with @ref from_key /
     * @ref from_mouse_button / @ref from_gamepad_button / @ref from_gamepad_axis rather than the members
     * directly; only the fields the chosen @ref source uses are meaningful.
     */
    struct action_binding
    {
        action_source source{action_source::key};
        key_code key_value{key_code::unknown};
        mouse_key_code mouse_value{mouse_key_code::left};
        gamepad_button_code gamepad_button_value{gamepad_button_code::unknown};
        gamepad_axis_code gamepad_axis_value{gamepad_axis_code::unknown};
        axis_sign sign{axis_sign::positive};
        float threshold{0.5f};

        static action_binding from_key(key_code code);
        static action_binding from_mouse_button(mouse_key_code code);
        static action_binding from_gamepad_button(gamepad_button_code code);
        static action_binding from_gamepad_axis(gamepad_axis_code axis, axis_sign sign, float threshold = 0.5f);
    };

    /** @brief Which physical control an @ref axis_binding reads. */
    enum class axis_source
    {
        gamepad_axis,
        key_pair, /**< @ref axis_binding::negative_key / @ref axis_binding::positive_key, -1 / 0 / +1. */
    };

    /**
     * @brief One physical control an axis reads from.
     *
     * An axis' value is the first binding (in registration order) that reads outside its dead zone, or 0 when
     * none does. Build with @ref from_gamepad_axis / @ref from_key_pair rather than the members directly.
     */
    struct axis_binding
    {
        axis_source source{axis_source::gamepad_axis};
        gamepad_axis_code gamepad_axis_value{gamepad_axis_code::unknown};
        float dead_zone{0.15f};
        float scale{1.0f};
        key_code negative_key{key_code::unknown};
        key_code positive_key{key_code::unknown};

        static axis_binding from_gamepad_axis(gamepad_axis_code axis, float dead_zone = 0.15f, float scale = 1.0f);
        static axis_binding from_key_pair(key_code negative, key_code positive);
    };

    /**
     * @brief A named layer on the @ref input context stack.
     *
     * While this context is on top, an action or axis whose name is not in @ref allowed_actions /
     * @ref allowed_axes resolves as inactive rather than falling through to the context below when
     * @ref blocks_lower is set (the default) — a modal menu, say, blocking gameplay input outright. With
     * @ref blocks_lower clear, an unlisted name is instead deferred to the next context down, all the way to
     * "no context pushed" (everything active). Leave both lists empty for a pure pass-through layer.
     */
    struct input_context
    {
        std::string name;
        std::vector<std::string> allowed_actions;
        std::vector<std::string> allowed_axes;
        bool blocks_lower{true};
    };

    /**
     * @brief Maps keyboard, mouse and gamepad input to named actions (digital: pressed / held / released this
     *        step) and axes (analog, dead-zoned). Engine subsystem, owned by @ref runtime::engine.
     *
     * Physical state (which keys / buttons are down, the cursor position and motion, gamepad axis values) is
     * kept live from the raw @c core::event_bus input events @ref rendering_engine::window already emits — this
     * subsystem never touches SDL itself, and never sees input the debug overlay is capturing, because
     * @c rendering_engine::window withholds those events from every listener the same way while it does.
     *
     * Gameplay code registers its actions and axes once — typically from a game module's bootstrap — with @ref
     * bind_action / @ref bind_axis, then polls @ref is_action_down / @ref get_axis / @ref mouse_position / @ref
     * mouse_delta instead of subscribing to raw input events. A name with an entry under @c input.bindings in
     * settings.json (@ref input_settings::bindings) has that entry parsed as its bindings in place of the ones
     * @ref bind_action / @ref bind_axis were called with; a malformed entry is warned about and the code-provided
     * bindings are kept instead.
     *
     * @ref was_action_pressed and @ref was_action_released answer for the fixed step that just ran: they latch
     * once per @ref runtime::engine::tick, right before its `core::frame` broadcast (see @ref begin_step), so
     * they stay stable through every behaviour's `on_fixed_update` in that step even when several run in one
     * rendered frame. @ref is_action_down, @ref get_axis, @ref mouse_position and @ref mouse_delta are live —
     * safe to poll from either `on_fixed_update` or the render-rate `on_update` — because the physical state
     * behind them only changes where @c rendering_engine::window pumps events, once per rendered frame and
     * before any fixed step drawn from it runs.
     *
     * Main-thread-only, like the rest of the engine.
     */
    struct input
    {
        input();
        ~input();

        input(const input&) = delete;
        input& operator=(const input&) = delete;
        input(input&&) = delete;
        input& operator=(input&&) = delete;

        /**
         * @brief Subscribes to the raw input events on @p bus and copies @p settings' binding overrides for
         *        @ref bind_action / @ref bind_axis to apply as actions and axes are registered.
         */
        void init(event_bus& bus, const input_settings& settings);

        /** @brief Drops the raw-event subscriptions and forgets every registered action, axis and context. */
        void quit();

        /**
         * @brief Registers (or replaces) a digital action.
         * @param name     Looked up later by @ref is_action_down / @ref was_action_pressed /
         *                 @ref was_action_released.
         * @param bindings The physical controls that drive it. A rebind from @ref input_settings::bindings
         *                 replaces this list entirely rather than adding to it.
         */
        void bind_action(std::string name, std::initializer_list<action_binding> bindings);

        /** @brief Registers (or replaces) an analog axis; see @ref bind_action for the parameters. */
        void bind_axis(std::string name, std::initializer_list<axis_binding> bindings);

        /** @brief Pushes @p context onto the stack; it becomes the top and its rules apply from this call on. */
        void push_context(input_context context);

        /** @brief Pops the top context. A no-op, warned about, when the stack is already empty. */
        void pop_context();

        /** @brief Name of the context on top of the stack, or empty with none pushed. */
        std::string_view top_context() const noexcept;

        /**
         * @brief Whether @p name's action is currently held.
         * @return false for a name nothing was bound to, or one the context stack currently blocks.
         */
        bool is_action_down(std::string_view name) const;

        /** @brief Whether @p name's action was pressed at any point during the fixed step that just ran. */
        bool was_action_pressed(std::string_view name) const;

        /** @brief Whether @p name's action was released at any point during the fixed step that just ran. */
        bool was_action_released(std::string_view name) const;

        /** @brief @p name's axis value in [-1, 1] (or [0, 1] for a trigger); 0 for an unknown or blocked name. */
        float get_axis(std::string_view name) const;

        /** @brief Cursor position in window coordinates, as last reported by @c rendering_engine::window. */
        core::math::vec2 mouse_position() const noexcept;

        /** @brief Cursor motion since the previous rendered frame, in window coordinates. */
        core::math::vec2 mouse_delta() const noexcept;

        /** @brief Scroll amount since the previous rendered frame (x horizontal, y vertical). */
        core::math::vec2 mouse_wheel_delta() const noexcept;

        /**
         * @brief Latches every action's pressed / released edges for the fixed step about to run and clears the
         *        accumulator behind them.
         *
         * Called once per fixed step by @ref runtime::engine::tick, right before its `core::frame` broadcast.
         * Not for game code to call.
         */
        void begin_step();

        /**
         * @brief Latches the cursor motion and scroll accumulated since the previous call into @ref mouse_delta /
         *        @ref mouse_wheel_delta and resets both accumulators.
         *
         * Called once per rendered frame by @ref runtime::engine::tick, right after @c rendering_engine::window
         * has pumped this frame's input. Not for game code to call.
         */
        void end_frame();

    private:
        static constexpr std::size_t k_key_count = static_cast<std::size_t>(key_code::count);
        static constexpr std::size_t k_mouse_button_count = static_cast<std::size_t>(mouse_key_code::x2) + 1;
        static constexpr std::size_t k_gamepad_button_count = static_cast<std::size_t>(gamepad_button_code::misc6) + 1;
        static constexpr std::size_t k_gamepad_axis_count =
            static_cast<std::size_t>(gamepad_axis_code::right_trigger) + 1;

        // Pairs a binding with the edge-detection state only a gamepad-axis binding needs: whether it was past
        // its threshold as of the previous update, so a value that stays past it does not re-fire "pressed".
        struct binding_slot
        {
            action_binding value;
            bool axis_was_active{false};
        };

        struct action_state
        {
            std::vector<binding_slot> bindings;
            bool pressed_since_sample{false};
            bool released_since_sample{false};
            bool pressed_this_step{false};
            bool released_this_step{false};
        };

        struct axis_state
        {
            std::vector<axis_binding> bindings;
        };

        struct gamepad_state
        {
            std::array<bool, k_gamepad_button_count> buttons{};
            std::array<float, k_gamepad_axis_count> axes{};
        };

        void on_key_down(const key_down& event);
        void on_key_up(const key_up& event);
        void on_mouse_key_down(const mouse_key_down& event);
        void on_mouse_key_up(const mouse_key_up& event);
        void on_mouse_move(const mouse_move& event);
        void on_mouse_wheel(const mouse_wheel& event);
        void on_gamepad_connected(const gamepad_connected& event);
        void on_gamepad_disconnected(const gamepad_disconnected& event);
        void on_gamepad_button(const gamepad_button& event);
        void on_gamepad_axis(const gamepad_axis& event);

        void mark_key_edge(key_code code, bool pressed);
        void mark_mouse_edge(mouse_key_code code, bool pressed);
        void mark_gamepad_button_edge(gamepad_button_code code, bool pressed);

        bool binding_active(const action_binding& binding) const;
        float axis_binding_value(const axis_binding& binding) const;
        bool is_allowed(std::string_view name, bool axis) const;

        std::unordered_map<std::string, action_state> m_actions;
        std::unordered_map<std::string, axis_state> m_axes;
        std::unordered_map<std::string, std::vector<std::string>> m_binding_overrides;
        std::vector<input_context> m_context_stack;

        std::array<bool, k_key_count> m_keys{};
        std::array<bool, k_mouse_button_count> m_mouse_buttons{};
        core::math::vec2 m_mouse_position;
        core::math::vec2 m_mouse_delta_accum;
        core::math::vec2 m_mouse_delta;
        core::math::vec2 m_mouse_wheel_delta_accum;
        core::math::vec2 m_mouse_wheel_delta;

        gamepad_state m_gamepad;
        int m_connected_gamepads{0};

        subscription m_key_down;
        subscription m_key_up;
        subscription m_mouse_key_down;
        subscription m_mouse_key_up;
        subscription m_mouse_move;
        subscription m_mouse_wheel;
        subscription m_gamepad_connected;
        subscription m_gamepad_disconnected;
        subscription m_gamepad_button;
        subscription m_gamepad_axis;
    };
} // namespace core
