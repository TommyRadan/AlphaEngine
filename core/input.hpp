// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file input.hpp
 * @brief Maps keyboard, mouse and gamepad input to named actions and axes, per local player.
 */

#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <core/event.hpp>
#include <core/math/math.hpp>
#include <core/string_id.hpp>
#include <core/subscription.hpp>

namespace core
{
    struct event_bus;
    class settings_registry;

    /** @brief Input / mouse configuration. */
    struct input_settings
    {
        /** @brief Mouse-look scale, radians per point of cursor travel. */
        float mouse_sensitivity{0.005f};
        bool mouse_reversed{false};

        /**
         * @brief Local player slots @ref input maintains (see @ref input::max_players). Every player has its
         *        own action and axis state; player 0 is what the single-player API (@ref input::is_action_down
         *        and friends, with no player argument) reads. Set from `input.max_players` in settings.json,
         *        @c ALPHAENGINE_INPUT_MAX_PLAYERS or @c --input-max-players, clamped to [1, 8].
         */
        unsigned int max_players{4};

        /**
         * @brief Rebinds for @c core::input actions and axes, from the `input.bindings` section of settings.json
         *        (e.g. `"move_forward": ["key:w", "gamepad_axis:left_y-"]`). Keyed by the action or axis name; a
         *        name registered through @c core::input::bind_action / @c bind_axis with an entry here uses
         *        these binding strings instead of its compiled defaults, for every player alike. Empty by
         *        default. See @c core/input.hpp for the binding-string grammar; an entry that does not parse is
         *        warned about and skipped. A player-specific rebind (@ref input::rebind_action /
         *        @ref input::listen_for_next_input) is a separate, per-user layer — see
         *        @ref input::load_user_bindings.
         */
        std::unordered_map<std::string, std::vector<std::string>> bindings;
    };

    /**
     * @brief Registers @ref input_settings's fields (`input.mouse_sensitivity`, `.mouse_reversed`,
     *        `.max_players`, `.bindings`) against @p out. Called once, before @c core::load_settings resolves
     *        the registry.
     */
    void register_settings(settings_registry& registry, input_settings& out);

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
     * "no context pushed" (everything active). Leave both lists empty for a pure pass-through layer. Shared by
     * every player: a context that blocks gameplay blocks it for all of them alike.
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
     *        step) and axes (analog, dead-zoned), for up to @ref max_players local players. Engine subsystem,
     *        owned by @ref runtime::engine.
     *
     * Physical state (which keys / buttons are down, the cursor position and motion, each connected gamepad's
     * axis values) is kept live from the raw @c core::event_bus input events @ref platform::window already
     * emits — this subsystem never touches the OS itself, and never sees input the debug overlay is capturing,
     * because @c platform::window withholds those events from every listener the same way while it does.
     *
     * **Players and devices.** Every player has its own action and axis state, evaluated only from the devices
     * currently assigned to it. The keyboard and the mouse are single, global devices: each belongs to exactly
     * one player at a time (both player 0 by default; see @ref assign_keyboard / @ref assign_mouse). A gamepad
     * is identified by its platform joystick instance id; connecting one assigns it to the player @ref
     * assign_gamepad last put it with, or, failing that, the lowest-indexed player with no gamepad yet, or
     * leaves it unassigned (logged) when every player already has one. Disconnecting a gamepad unassigns it,
     * releasing whatever it was holding down for its player, but remembers the player for next time (an
     * explicit @ref unassign_gamepad forgets it instead). Every reassignment — connect, disconnect, or an
     * explicit call — broadcasts @ref gamepad_assignment_changed on the bus @ref init was handed.
     *
     * **Bindings.** @ref bind_action / @ref bind_axis register one name's bindings for every player alike (a
     * `input.bindings` entry from settings.json replaces them for every player too); @ref rebind_action / @ref
     * rebind_axis replace one player's bindings for a name without touching the others, and @ref
     * listen_for_next_input drives that from the next physical input the player's own devices report rather
     * than a caller-supplied list. A rebind survives only in memory unless saved: @ref load_user_bindings /
     * @ref save_user_bindings read and write the per-player rebinds (only, not the shared defaults) as JSON at
     * a caller-given path — core stays free of SDL and platform/, so the caller resolves the per-user
     * preference directory and hands this the full path.
     *
     * Gameplay code registers its actions and axes once — typically from a game module's bootstrap — with @ref
     * bind_action / @ref bind_axis, then polls @ref is_action_down / @ref get_axis / @ref mouse_position / @ref
     * mouse_delta instead of subscribing to raw input events. The overloads with no player argument read player
     * 0, so single-player code is unaffected by any of the above.
     *
     * @ref was_action_pressed and @ref was_action_released answer for the fixed step that just ran: they latch
     * once per fixed step, before any of the step's game logic runs (see @ref begin_step), so they stay stable
     * through every behaviour's `on_fixed_update` in that step even when several run in one rendered frame. @ref
     * is_action_down, @ref get_axis, @ref mouse_position and @ref mouse_delta are live — safe to poll from either
     * `on_fixed_update` or the render-rate `on_update` — because the physical state behind them only changes where @c
     * platform::window pumps events, once per rendered frame and before any fixed step drawn from it runs.
     *
     * Main-thread-only, like the rest of the engine.
     */
    struct input
    {
        /** @brief The player index the single-argument overloads (and the keyboard / mouse by default) use. */
        static constexpr int k_default_player = 0;

        /** @brief Not a valid player index: "no player" for a gamepad's assignment, or an out-of-range query. */
        static constexpr int k_no_player = -1;

        input();
        ~input();

        input(const input&) = delete;
        input& operator=(const input&) = delete;
        input(input&&) = delete;
        input& operator=(input&&) = delete;

        /**
         * @brief Subscribes to the raw input events on @p bus, sizes the player array from
         *        @c input_settings::max_players (at least 1), and copies @p settings' binding overrides for
         *        @ref bind_action / @ref bind_axis to apply, for every player, as actions and axes are
         *        registered. The keyboard and the mouse start out assigned to player 0.
         */
        void init(event_bus& bus, const input_settings& settings);

        /** @brief Drops the raw-event subscriptions and forgets every player, action, axis, context and device
         *         assignment. */
        void quit();

        /** @brief Number of player slots (@c input_settings::max_players as resolved by @ref init). */
        int max_players() const noexcept;

        // -- Device assignment -------------------------------------------

        /** @brief Assigns the keyboard to @p player, out of range warned about and ignored. Whatever key-bound
         *         action the previous owner was holding down is released for it. */
        void assign_keyboard(int player);

        /** @brief Assigns the mouse to @p player; see @ref assign_keyboard. */
        void assign_mouse(int player);

        /** @brief The player the keyboard currently drives. */
        int keyboard_player() const noexcept;

        /** @brief The player the mouse currently drives. */
        int mouse_player() const noexcept;

        /**
         * @brief Assigns the connected gamepad @p gamepad_id to @p player.
         * @return false, logged, when @p player is out of range, @p gamepad_id is not currently connected, or
         *         @p player already has a different gamepad (unassign it first).
         */
        bool assign_gamepad(std::uint32_t gamepad_id, int player);

        /** @brief Unassigns @p gamepad_id from its player, if any, and forgets that pairing (unlike a
         *         disconnect, a later reconnect of this id starts unassigned again). A no-op, silently, when
         *         @p gamepad_id has no player. */
        void unassign_gamepad(std::uint32_t gamepad_id);

        /** @brief The player @p gamepad_id currently drives, or @ref k_no_player. */
        int gamepad_player(std::uint32_t gamepad_id) const noexcept;

        /** @brief The gamepad id currently assigned to @p player, or @c std::nullopt. */
        std::optional<std::uint32_t> player_gamepad(int player) const noexcept;

        /**
         * @brief Registers (or replaces) a digital action for every player.
         * @param name     Looked up later by @ref is_action_down / @ref was_action_pressed /
         *                 @ref was_action_released.
         * @param bindings The physical controls that drive it. A rebind from @ref input_settings::bindings
         *                 replaces this list entirely rather than adding to it; a per-player rebind from @ref
         *                 load_user_bindings is then applied on top for the player(s) it names.
         */
        void bind_action(std::string name, std::initializer_list<action_binding> bindings);

        /** @brief Registers (or replaces) an analog axis for every player; see @ref bind_action. */
        void bind_axis(std::string name, std::initializer_list<axis_binding> bindings);

        /**
         * @brief Replaces @p player's bindings for the already-registered action @p name, leaving every other
         *        player's untouched. Marks it as one of @p player's saved rebinds for @ref save_user_bindings.
         */
        void rebind_action(int player, std::string_view name, std::vector<action_binding> bindings);

        /** @brief Replaces @p player's bindings for the already-registered axis @p name; see @ref
         *         rebind_action. */
        void rebind_axis(int player, std::string_view name, std::vector<axis_binding> bindings);

        /** @brief Pushes @p context onto the stack; it becomes the top and its rules apply from this call on. */
        void push_context(input_context context);

        /** @brief Pops the top context. A no-op, warned about, when the stack is already empty. */
        void pop_context();

        /** @brief Name of the context on top of the stack, or empty with none pushed. */
        std::string_view top_context() const noexcept;

        /** @brief Whether player 0's @p name action is currently held; see the two-argument overload. */
        bool is_action_down(std::string_view name) const;

        /**
         * @brief Whether @p player's @p name action is currently held.
         * @return false for an out-of-range player, a name nothing was bound to, or one the context stack
         *         currently blocks.
         */
        bool is_action_down(int player, std::string_view name) const;

        /** @brief Whether player 0's @p name action was pressed during the fixed step that just ran. */
        bool was_action_pressed(std::string_view name) const;

        /** @brief Whether @p player's @p name action was pressed during the fixed step that just ran. */
        bool was_action_pressed(int player, std::string_view name) const;

        /** @brief Whether player 0's @p name action was released during the fixed step that just ran. */
        bool was_action_released(std::string_view name) const;

        /** @brief Whether @p player's @p name action was released during the fixed step that just ran. */
        bool was_action_released(int player, std::string_view name) const;

        /** @brief Player 0's @p name axis value; see the two-argument overload. */
        float get_axis(std::string_view name) const;

        /** @brief @p player's @p name axis value in [-1, 1] (or [0, 1] for a trigger); 0 for an out-of-range
         *         player, an unknown name, or one the context stack currently blocks. */
        float get_axis(int player, std::string_view name) const;

        /** @brief Cursor position in window coordinates, as last reported by @c platform::window. */
        core::math::vec2 mouse_position() const noexcept;

        /** @brief Cursor motion since the previous rendered frame, in window coordinates. */
        core::math::vec2 mouse_delta() const noexcept;

        /** @brief Scroll amount since the previous rendered frame (x horizontal, y vertical). */
        core::math::vec2 mouse_wheel_delta() const noexcept;

        /** @brief Called from @ref listen_for_next_input once a binding was captured (@c true) or the listen
         *         was cancelled (@c false, @ref cancel_listen or a disallowed player). */
        using listen_callback = std::function<void(bool captured)>;

        /**
         * @brief Captures the next physical control one of @p player's own devices reports and rebinds @p name
         *        to just that control (@ref rebind_action or @ref rebind_axis, depending on @p is_axis),
         *        replacing this call's own listen if one was already pending.
         *
         * An action (@c is_axis false) captures a key press, a mouse button press, a gamepad button press, or a
         * gamepad axis crossing its default threshold (as a digital trigger, signed the way it was crossed). An
         * axis (@c is_axis true) captures a gamepad axis crossing the same threshold, as its full analog range;
         * capturing an axis from a key pair is not supported — bind one with @ref rebind_axis directly. Only
         * @p player's currently assigned devices are read: a key or mouse-button press is ignored unless
         * @p player owns that device, and likewise a gamepad's input unless it is the one assigned to
         * @p player.
         * @param callback Called once, synchronously from the capturing raw-input handler, with the outcome;
         *                 may be empty.
         */
        void listen_for_next_input(int player, std::string_view name, bool is_axis, listen_callback callback = {});

        /** @brief Whether a @ref listen_for_next_input call is still waiting for its input. */
        bool is_listening() const noexcept;

        /** @brief Cancels a pending @ref listen_for_next_input, calling its callback with @c false; a no-op
         *         when none is pending. */
        void cancel_listen();

        /**
         * @brief Reads @p path (JSON; see @ref save_user_bindings for the shape) and remembers every
         *        (player, name) rebind it names, applying each to the matching player as its action or axis is
         *        next registered through @ref bind_action / @ref bind_axis — so this is meant to be called
         *        before the game's actions and axes are bound, typically right after @ref init.
         * @return false, logged, when @p path cannot be read or is not valid JSON in the expected shape;
         *         @c core::input keeps whatever bindings it already had.
         */
        bool load_user_bindings(const std::filesystem::path& path);

        /**
         * @brief Writes every player's rebinds — the ones @ref rebind_action / @ref rebind_axis (directly, or
         *        through @ref listen_for_next_input or @ref load_user_bindings) have set, not the shared
         *        defaults @ref bind_action / @ref bind_axis registered — to @p path as JSON:
         *        `{"players": {"<index>": {"actions": {"<name>": ["<binding string>", ...]}, "axes": {...}}}}`,
         *        the same binding-string grammar @ref input_settings::bindings uses. A binding
         *        @ref load_user_bindings' inverse cannot spell (none in this version) is left out of its list.
         * @return false, logged, when @p path cannot be written.
         */
        bool save_user_bindings(const std::filesystem::path& path) const;

        /**
         * @brief Latches every player's action pressed / released edges for the fixed step about to run and
         *        clears the accumulator behind them.
         *
         * Called once per fixed step by the engine's first system of the step (@c runtime::stage::scripts_fixed),
         * before any of the step's game logic. Not for game code to call.
         */
        void begin_step();

        /**
         * @brief Latches the cursor motion and scroll accumulated since the previous call into @ref mouse_delta /
         *        @ref mouse_wheel_delta and resets both accumulators.
         *
         * Called once per rendered frame by the engine's input stage (@c runtime::stage::input), right after
         * @c platform::window has pumped this frame's input. Not for game code to call.
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

        // One connected gamepad's raw physical state, keyed by its platform joystick instance id, independent
        // of which player (if any) it is currently assigned to.
        struct gamepad_device_state
        {
            std::array<bool, k_gamepad_button_count> buttons{};
            std::array<float, k_gamepad_axis_count> axes{};
        };

        // One local player's own action / axis state and the gamepad, if any, driving it. A name always has an
        // entry here once bind_action / bind_axis has registered it (identical to every other player's unless
        // rebind_action / rebind_axis later replaced this player's own copy).
        struct player_data
        {
            std::unordered_map<core::string_id, action_state> actions;
            std::unordered_map<core::string_id, axis_state> axes;
            std::unordered_set<core::string_id> rebound_actions; /**< Names @ref save_user_bindings writes out. */
            std::unordered_set<core::string_id> rebound_axes;
            std::optional<std::uint32_t> assigned_gamepad;
        };

        // A per-player rebind read by load_user_bindings, held as raw binding-string tokens (the grammar
        // parses against the engine's enums, not against JSON) until the matching bind_action / bind_axis call
        // applies it.
        struct pending_player_bindings
        {
            std::unordered_map<std::string, std::vector<std::string>> actions;
            std::unordered_map<std::string, std::vector<std::string>> axes;
        };

        struct listen_state
        {
            int player{k_default_player};
            core::string_id name;
            bool is_axis{false};
            listen_callback callback;
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

        void mark_key_edge(int player, key_code code, bool pressed);
        void mark_mouse_edge(int player, mouse_key_code code, bool pressed);
        void mark_gamepad_button_edge(int player, gamepad_button_code code, bool pressed);

        void set_gamepad_owner(std::uint32_t gamepad_id, int previous_player, int player);
        const gamepad_device_state* gamepad_device_for_player(int player) const;

        void try_capture_listen_from_key(key_code code, bool pressed);
        void try_capture_listen_from_mouse_button(mouse_key_code code, bool pressed);
        void try_capture_listen_from_gamepad_button(std::uint32_t gamepad_id, gamepad_button_code code, bool pressed);
        void try_capture_listen_from_gamepad_axis(std::uint32_t gamepad_id, gamepad_axis_code axis, float value);
        void complete_listen(const action_binding& binding);
        void complete_listen(const axis_binding& binding);

        action_state build_action_state(const std::string& name, std::initializer_list<action_binding> bindings) const;
        axis_state build_axis_state(const std::string& name, std::initializer_list<axis_binding> bindings) const;
        void apply_pending_action_rebind(core::string_id id, const std::string& name);
        void apply_pending_axis_rebind(core::string_id id, const std::string& name);

        bool valid_player(int player) const noexcept;
        bool binding_active(int player, const action_binding& binding) const;
        float axis_binding_value(int player, const axis_binding& binding) const;
        bool is_allowed(std::string_view name, bool axis) const;

        std::vector<player_data> m_players;
        std::unordered_map<std::string, std::vector<std::string>> m_binding_overrides;
        std::unordered_map<int, pending_player_bindings> m_pending_user_bindings;
        std::vector<input_context> m_context_stack;
        std::optional<listen_state> m_listen;

        std::array<bool, k_key_count> m_keys{};
        std::array<bool, k_mouse_button_count> m_mouse_buttons{};
        core::math::vec2 m_mouse_position;
        core::math::vec2 m_mouse_delta_accum;
        core::math::vec2 m_mouse_delta;
        core::math::vec2 m_mouse_wheel_delta_accum;
        core::math::vec2 m_mouse_wheel_delta;
        int m_keyboard_player{k_default_player};
        int m_mouse_player{k_default_player};

        std::unordered_map<std::uint32_t, gamepad_device_state> m_gamepad_devices;
        std::unordered_map<std::uint32_t, int> m_gamepad_owner;
        std::unordered_map<std::uint32_t, int> m_gamepad_last_player;

        event_bus* m_bus{nullptr};

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
