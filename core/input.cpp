// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/input.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include <core/event_engine.hpp>
#include <core/log.hpp>
#include <core/os/os.hpp>
#include <core/settings_registry.hpp>

namespace core
{
    namespace
    {
        std::string to_lower(std::string_view text)
        {
            std::string out{text};
            for (char& c : out)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return out;
        }

        bool contains(const std::vector<std::string>& list, std::string_view name)
        {
            return std::find(list.begin(), list.end(), name) != list.end();
        }

        bool crosses_threshold(float value, axis_sign sign, float threshold) noexcept
        {
            return sign == axis_sign::positive ? value >= threshold : value <= -threshold;
        }

        // Rescales the region outside [-dead_zone, dead_zone] to fill [-1, 1] again, so a stick that has just
        // left the dead zone reads as a small value rather than jumping straight to it.
        float apply_dead_zone(float value, float dead_zone) noexcept
        {
            dead_zone = std::clamp(dead_zone, 0.0f, 0.99f);
            const float magnitude = std::abs(value);
            if (magnitude <= dead_zone)
            {
                return 0.0f;
            }
            const float rescaled = std::clamp((magnitude - dead_zone) / (1.0f - dead_zone), 0.0f, 1.0f);
            return std::copysign(rescaled, value);
        }

        // -- Binding-string name tables -----------------------------------
        //
        // Every name matches the corresponding enumerator's own spelling (case-insensitively), so a
        // settings.json / user-bindings-file author can read the enum in core/event.hpp to know what is
        // bindable. Shared by the parse_* (name -> code) and *_name (code -> name) directions below.

        constexpr std::pair<std::string_view, key_code> k_named_keys[] = {
            {"enter", key_code::enter},
            {"escape", key_code::escape},
            {"backspace", key_code::backspace},
            {"tab", key_code::tab},
            {"space", key_code::space},
            {"insert", key_code::insert},
            {"del", key_code::del},
            {"home", key_code::home},
            {"end", key_code::end},
            {"page_up", key_code::page_up},
            {"page_down", key_code::page_down},
            {"left", key_code::left},
            {"right", key_code::right},
            {"up", key_code::up},
            {"down", key_code::down},
            {"left_shift", key_code::left_shift},
            {"right_shift", key_code::right_shift},
            {"left_ctrl", key_code::left_ctrl},
            {"right_ctrl", key_code::right_ctrl},
            {"left_alt", key_code::left_alt},
            {"right_alt", key_code::right_alt},
            {"left_gui", key_code::left_gui},
            {"right_gui", key_code::right_gui},
            {"caps_lock", key_code::caps_lock},
            {"num_lock", key_code::num_lock},
            {"scroll_lock", key_code::scroll_lock},
            {"print_screen", key_code::print_screen},
            {"pause", key_code::pause},
            {"menu", key_code::menu},
            {"minus", key_code::minus},
            {"equals", key_code::equals},
            {"left_bracket", key_code::left_bracket},
            {"right_bracket", key_code::right_bracket},
            {"backslash", key_code::backslash},
            {"semicolon", key_code::semicolon},
            {"apostrophe", key_code::apostrophe},
            {"grave", key_code::grave},
            {"comma", key_code::comma},
            {"period", key_code::period},
            {"slash", key_code::slash},
            {"keypad_divide", key_code::keypad_divide},
            {"keypad_multiply", key_code::keypad_multiply},
            {"keypad_minus", key_code::keypad_minus},
            {"keypad_plus", key_code::keypad_plus},
            {"keypad_enter", key_code::keypad_enter},
            {"keypad_period", key_code::keypad_period},
            {"keypad_equals", key_code::keypad_equals},
        };

        constexpr std::pair<std::string_view, mouse_key_code> k_named_mouse_buttons[] = {
            {"left", mouse_key_code::left},
            {"right", mouse_key_code::right},
            {"middle", mouse_key_code::middle},
            {"x1", mouse_key_code::x1},
            {"x2", mouse_key_code::x2},
        };

        constexpr std::pair<std::string_view, gamepad_button_code> k_named_gamepad_buttons[] = {
            {"south", gamepad_button_code::south},
            {"east", gamepad_button_code::east},
            {"west", gamepad_button_code::west},
            {"north", gamepad_button_code::north},
            {"back", gamepad_button_code::back},
            {"guide", gamepad_button_code::guide},
            {"start", gamepad_button_code::start},
            {"left_stick", gamepad_button_code::left_stick},
            {"right_stick", gamepad_button_code::right_stick},
            {"left_shoulder", gamepad_button_code::left_shoulder},
            {"right_shoulder", gamepad_button_code::right_shoulder},
            {"dpad_up", gamepad_button_code::dpad_up},
            {"dpad_down", gamepad_button_code::dpad_down},
            {"dpad_left", gamepad_button_code::dpad_left},
            {"dpad_right", gamepad_button_code::dpad_right},
            {"misc1", gamepad_button_code::misc1},
            {"right_paddle1", gamepad_button_code::right_paddle1},
            {"left_paddle1", gamepad_button_code::left_paddle1},
            {"right_paddle2", gamepad_button_code::right_paddle2},
            {"left_paddle2", gamepad_button_code::left_paddle2},
            {"touchpad", gamepad_button_code::touchpad},
            {"misc2", gamepad_button_code::misc2},
            {"misc3", gamepad_button_code::misc3},
            {"misc4", gamepad_button_code::misc4},
            {"misc5", gamepad_button_code::misc5},
            {"misc6", gamepad_button_code::misc6},
        };

        constexpr std::pair<std::string_view, gamepad_axis_code> k_named_gamepad_axes[] = {
            {"left_x", gamepad_axis_code::left_x},
            {"left_y", gamepad_axis_code::left_y},
            {"right_x", gamepad_axis_code::right_x},
            {"right_y", gamepad_axis_code::right_y},
            {"left_trigger", gamepad_axis_code::left_trigger},
            {"right_trigger", gamepad_axis_code::right_trigger},
        };

        std::optional<key_code> parse_key_code_name(std::string_view name)
        {
            if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z')
            {
                return static_cast<key_code>(static_cast<int>(key_code::a) + (name[0] - 'a'));
            }
            if (name.size() == 5 && name.substr(0, 4) == "num_" && std::isdigit(static_cast<unsigned char>(name[4])))
            {
                return static_cast<key_code>(static_cast<int>(key_code::num_0) + (name[4] - '0'));
            }
            if (name.size() == 8 && name.substr(0, 7) == "keypad_" && std::isdigit(static_cast<unsigned char>(name[7])))
            {
                return static_cast<key_code>(static_cast<int>(key_code::keypad_0) + (name[7] - '0'));
            }
            if (name.size() >= 2 && name.size() <= 3 && name[0] == 'f' &&
                std::all_of(
                    name.begin() + 1, name.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
            {
                int number = 0;
                for (const char c : name.substr(1))
                {
                    number = (number * 10) + (c - '0');
                }
                if (number >= 1 && number <= 12)
                {
                    return static_cast<key_code>(static_cast<int>(key_code::f1) + (number - 1));
                }
            }

            for (const auto& [key_name, code] : k_named_keys)
            {
                if (key_name == name)
                {
                    return code;
                }
            }
            return std::nullopt;
        }

        std::optional<mouse_key_code> parse_mouse_key_code_name(std::string_view name)
        {
            for (const auto& [button_name, code] : k_named_mouse_buttons)
            {
                if (button_name == name)
                {
                    return code;
                }
            }
            return std::nullopt;
        }

        std::optional<gamepad_button_code> parse_gamepad_button_name(std::string_view name)
        {
            for (const auto& [button_name, code] : k_named_gamepad_buttons)
            {
                if (button_name == name)
                {
                    return code;
                }
            }
            return std::nullopt;
        }

        std::optional<gamepad_axis_code> parse_gamepad_axis_name(std::string_view name)
        {
            for (const auto& [axis_name, code] : k_named_gamepad_axes)
            {
                if (axis_name == name)
                {
                    return code;
                }
            }
            return std::nullopt;
        }

        // -- Reverse: engine code -> canonical binding-string name --------
        //
        // The inverse of the parse_* functions above, for save_user_bindings; a code with no name (unknown, or
        // outside every table and algorithmic range) yields std::nullopt rather than a guess.

        std::optional<std::string> key_code_name(key_code code)
        {
            if (code >= key_code::a && code <= key_code::z)
            {
                return std::string(1,
                                   static_cast<char>('a' + (static_cast<int>(code) - static_cast<int>(key_code::a))));
            }
            if (code >= key_code::num_0 && code <= key_code::num_9)
            {
                return "num_" + std::to_string(static_cast<int>(code) - static_cast<int>(key_code::num_0));
            }
            if (code >= key_code::keypad_0 && code <= key_code::keypad_9)
            {
                return "keypad_" + std::to_string(static_cast<int>(code) - static_cast<int>(key_code::keypad_0));
            }
            if (code >= key_code::f1 && code <= key_code::f12)
            {
                return "f" + std::to_string(static_cast<int>(code) - static_cast<int>(key_code::f1) + 1);
            }
            for (const auto& [key_name, named_code] : k_named_keys)
            {
                if (named_code == code)
                {
                    return std::string{key_name};
                }
            }
            return std::nullopt;
        }

        std::optional<std::string> mouse_key_code_name(mouse_key_code code)
        {
            for (const auto& [button_name, named_code] : k_named_mouse_buttons)
            {
                if (named_code == code)
                {
                    return std::string{button_name};
                }
            }
            return std::nullopt;
        }

        std::optional<std::string> gamepad_button_name(gamepad_button_code code)
        {
            for (const auto& [button_name, named_code] : k_named_gamepad_buttons)
            {
                if (named_code == code)
                {
                    return std::string{button_name};
                }
            }
            return std::nullopt;
        }

        std::optional<std::string> gamepad_axis_name(gamepad_axis_code code)
        {
            for (const auto& [axis_name, named_code] : k_named_gamepad_axes)
            {
                if (named_code == code)
                {
                    return std::string{axis_name};
                }
            }
            return std::nullopt;
        }

        // Binding-string grammar (case-insensitive):
        //   key:<name>                    -- action only
        //   mouse_button:<name>            -- action only
        //   gamepad_button:<name>          -- action only
        //   gamepad_axis:<name>+/-         -- action only: digital trigger past the default threshold
        //   gamepad_axis:<name>            -- axis only: full analog range
        //   key_pair:<negative>,<positive> -- axis only: composite from two keys
        std::optional<action_binding> parse_action_binding(std::string_view token)
        {
            const std::string lowered = to_lower(token);
            const std::size_t colon = lowered.find(':');
            if (colon == std::string::npos)
            {
                return std::nullopt;
            }
            const std::string_view kind{lowered.data(), colon};
            const std::string_view rest{lowered.data() + colon + 1, lowered.size() - colon - 1};

            if (kind == "key")
            {
                if (const auto code = parse_key_code_name(rest))
                {
                    return action_binding::from_key(*code);
                }
            }
            else if (kind == "mouse_button")
            {
                if (const auto code = parse_mouse_key_code_name(rest))
                {
                    return action_binding::from_mouse_button(*code);
                }
            }
            else if (kind == "gamepad_button")
            {
                if (const auto code = parse_gamepad_button_name(rest))
                {
                    return action_binding::from_gamepad_button(*code);
                }
            }
            else if (kind == "gamepad_axis")
            {
                if (!rest.empty() && (rest.back() == '+' || rest.back() == '-'))
                {
                    const axis_sign sign = rest.back() == '+' ? axis_sign::positive : axis_sign::negative;
                    if (const auto axis = parse_gamepad_axis_name(rest.substr(0, rest.size() - 1)))
                    {
                        return action_binding::from_gamepad_axis(*axis, sign);
                    }
                }
            }
            return std::nullopt;
        }

        std::optional<axis_binding> parse_axis_binding(std::string_view token)
        {
            const std::string lowered = to_lower(token);
            const std::size_t colon = lowered.find(':');
            if (colon == std::string::npos)
            {
                return std::nullopt;
            }
            const std::string_view kind{lowered.data(), colon};
            const std::string_view rest{lowered.data() + colon + 1, lowered.size() - colon - 1};

            if (kind == "gamepad_axis")
            {
                if (const auto axis = parse_gamepad_axis_name(rest))
                {
                    return axis_binding::from_gamepad_axis(*axis);
                }
            }
            else if (kind == "key_pair")
            {
                const std::size_t comma = rest.find(',');
                if (comma != std::string_view::npos)
                {
                    const auto negative = parse_key_code_name(rest.substr(0, comma));
                    const auto positive = parse_key_code_name(rest.substr(comma + 1));
                    if (negative.has_value() && positive.has_value())
                    {
                        return axis_binding::from_key_pair(*negative, *positive);
                    }
                }
            }
            return std::nullopt;
        }

        std::optional<std::string> action_binding_to_string(const action_binding& binding)
        {
            switch (binding.source)
            {
            case action_source::key:
                if (const auto name = key_code_name(binding.key_value))
                {
                    return "key:" + *name;
                }
                return std::nullopt;
            case action_source::mouse_button:
                if (const auto name = mouse_key_code_name(binding.mouse_value))
                {
                    return "mouse_button:" + *name;
                }
                return std::nullopt;
            case action_source::gamepad_button:
                if (const auto name = gamepad_button_name(binding.gamepad_button_value))
                {
                    return "gamepad_button:" + *name;
                }
                return std::nullopt;
            case action_source::gamepad_axis:
                if (const auto name = gamepad_axis_name(binding.gamepad_axis_value))
                {
                    return "gamepad_axis:" + *name + (binding.sign == axis_sign::positive ? "+" : "-");
                }
                return std::nullopt;
            }
            return std::nullopt;
        }

        std::optional<std::string> axis_binding_to_string(const axis_binding& binding)
        {
            switch (binding.source)
            {
            case axis_source::gamepad_axis:
                if (const auto name = gamepad_axis_name(binding.gamepad_axis_value))
                {
                    return "gamepad_axis:" + *name;
                }
                return std::nullopt;
            case axis_source::key_pair:
            {
                const auto negative = key_code_name(binding.negative_key);
                const auto positive = key_code_name(binding.positive_key);
                if (negative.has_value() && positive.has_value())
                {
                    return "key_pair:" + *negative + "," + *positive;
                }
                return std::nullopt;
            }
            }
            return std::nullopt;
        }
    } // namespace

    action_binding action_binding::from_key(key_code code)
    {
        action_binding binding;
        binding.source = action_source::key;
        binding.key_value = code;
        return binding;
    }

    action_binding action_binding::from_mouse_button(mouse_key_code code)
    {
        action_binding binding;
        binding.source = action_source::mouse_button;
        binding.mouse_value = code;
        return binding;
    }

    action_binding action_binding::from_gamepad_button(gamepad_button_code code)
    {
        action_binding binding;
        binding.source = action_source::gamepad_button;
        binding.gamepad_button_value = code;
        return binding;
    }

    action_binding action_binding::from_gamepad_axis(gamepad_axis_code axis, axis_sign sign, float threshold)
    {
        action_binding binding;
        binding.source = action_source::gamepad_axis;
        binding.gamepad_axis_value = axis;
        binding.sign = sign;
        binding.threshold = threshold;
        return binding;
    }

    axis_binding axis_binding::from_gamepad_axis(gamepad_axis_code axis, float dead_zone, float scale)
    {
        axis_binding binding;
        binding.source = axis_source::gamepad_axis;
        binding.gamepad_axis_value = axis;
        binding.dead_zone = dead_zone;
        binding.scale = scale;
        return binding;
    }

    axis_binding axis_binding::from_key_pair(key_code negative, key_code positive)
    {
        axis_binding binding;
        binding.source = axis_source::key_pair;
        binding.negative_key = negative;
        binding.positive_key = positive;
        return binding;
    }

    input::input() = default;
    input::~input() = default;

    void input::init(event_bus& bus, const input_settings& settings)
    {
        m_bus = &bus;
        m_binding_overrides = settings.bindings;
        const unsigned int player_count = std::max(1u, settings.max_players);
        m_players.assign(player_count, player_data{});
        m_keyboard_player = k_default_player;
        m_mouse_player = k_default_player;

        m_key_down = bus.subscribe<key_down>([this](const key_down& event) { on_key_down(event); });
        m_key_up = bus.subscribe<key_up>([this](const key_up& event) { on_key_up(event); });
        m_mouse_key_down =
            bus.subscribe<mouse_key_down>([this](const mouse_key_down& event) { on_mouse_key_down(event); });
        m_mouse_key_up = bus.subscribe<mouse_key_up>([this](const mouse_key_up& event) { on_mouse_key_up(event); });
        m_mouse_move = bus.subscribe<mouse_move>([this](const mouse_move& event) { on_mouse_move(event); });
        m_mouse_wheel = bus.subscribe<mouse_wheel>([this](const mouse_wheel& event) { on_mouse_wheel(event); });
        m_gamepad_connected =
            bus.subscribe<gamepad_connected>([this](const gamepad_connected& event) { on_gamepad_connected(event); });
        m_gamepad_disconnected = bus.subscribe<gamepad_disconnected>([this](const gamepad_disconnected& event)
                                                                     { on_gamepad_disconnected(event); });
        m_gamepad_button =
            bus.subscribe<gamepad_button>([this](const gamepad_button& event) { on_gamepad_button(event); });
        m_gamepad_axis = bus.subscribe<gamepad_axis>([this](const gamepad_axis& event) { on_gamepad_axis(event); });

        LOG_INF("Init core::input (%d player(s), %zu binding override(s) from settings)",
                static_cast<int>(m_players.size()),
                m_binding_overrides.size());
    }

    void input::quit()
    {
        m_key_down.reset();
        m_key_up.reset();
        m_mouse_key_down.reset();
        m_mouse_key_up.reset();
        m_mouse_move.reset();
        m_mouse_wheel.reset();
        m_gamepad_connected.reset();
        m_gamepad_disconnected.reset();
        m_gamepad_button.reset();
        m_gamepad_axis.reset();

        m_players.clear();
        m_binding_overrides.clear();
        m_pending_user_bindings.clear();
        m_context_stack.clear();
        m_listen.reset();

        m_keys.fill(false);
        m_mouse_buttons.fill(false);
        m_mouse_position = core::math::vec2{};
        m_mouse_delta_accum = core::math::vec2{};
        m_mouse_delta = core::math::vec2{};
        m_mouse_wheel_delta_accum = core::math::vec2{};
        m_mouse_wheel_delta = core::math::vec2{};
        m_keyboard_player = k_default_player;
        m_mouse_player = k_default_player;

        m_gamepad_devices.clear();
        m_gamepad_owner.clear();
        m_gamepad_last_player.clear();

        m_bus = nullptr;

        LOG_INF("Quit core::input");
    }

    int input::max_players() const noexcept
    {
        return static_cast<int>(m_players.size());
    }

    bool input::valid_player(int player) const noexcept
    {
        return player >= 0 && player < static_cast<int>(m_players.size());
    }

    void input::assign_keyboard(int player)
    {
        if (!valid_player(player))
        {
            LOG_WRN("input: assign_keyboard: player %d is out of range (max %d)", player, max_players());
            return;
        }
        if (player == m_keyboard_player)
        {
            return;
        }
        for (std::size_t i = 0; i < m_keys.size(); ++i)
        {
            if (m_keys[i])
            {
                mark_key_edge(m_keyboard_player, static_cast<key_code>(i), false);
            }
        }
        LOG_INF("input: keyboard reassigned from player %d to player %d", m_keyboard_player, player);
        m_keyboard_player = player;
    }

    void input::assign_mouse(int player)
    {
        if (!valid_player(player))
        {
            LOG_WRN("input: assign_mouse: player %d is out of range (max %d)", player, max_players());
            return;
        }
        if (player == m_mouse_player)
        {
            return;
        }
        for (std::size_t i = 0; i < m_mouse_buttons.size(); ++i)
        {
            if (m_mouse_buttons[i])
            {
                mark_mouse_edge(m_mouse_player, static_cast<mouse_key_code>(i), false);
            }
        }
        LOG_INF("input: mouse reassigned from player %d to player %d", m_mouse_player, player);
        m_mouse_player = player;
    }

    int input::keyboard_player() const noexcept
    {
        return m_keyboard_player;
    }

    int input::mouse_player() const noexcept
    {
        return m_mouse_player;
    }

    void input::set_gamepad_owner(std::uint32_t gamepad_id, int previous_player, int player)
    {
        if (player != k_no_player)
        {
            m_gamepad_owner[gamepad_id] = player;
            m_players[static_cast<std::size_t>(player)].assigned_gamepad = gamepad_id;
            m_gamepad_last_player[gamepad_id] = player;
        }
        else
        {
            m_gamepad_owner.erase(gamepad_id);
        }
        if (previous_player != k_no_player && previous_player != player)
        {
            m_players[static_cast<std::size_t>(previous_player)].assigned_gamepad.reset();
        }
        LOG_INF("input: gamepad id=%u assigned to player %d (was %d)", gamepad_id, player, previous_player);
        if (m_bus != nullptr)
        {
            m_bus->emit<gamepad_assignment_changed>(gamepad_id, previous_player, player);
        }
    }

    bool input::assign_gamepad(std::uint32_t gamepad_id, int player)
    {
        if (!valid_player(player))
        {
            LOG_WRN("input: assign_gamepad: player %d is out of range (max %d)", player, max_players());
            return false;
        }
        if (m_gamepad_devices.find(gamepad_id) == m_gamepad_devices.end())
        {
            LOG_WRN("input: assign_gamepad: gamepad id=%u is not connected", gamepad_id);
            return false;
        }
        const auto current_owner = m_gamepad_owner.find(gamepad_id);
        const int previous_player = current_owner != m_gamepad_owner.end() ? current_owner->second : k_no_player;
        if (previous_player == player)
        {
            return true;
        }
        if (m_players[static_cast<std::size_t>(player)].assigned_gamepad.has_value())
        {
            LOG_WRN("input: assign_gamepad: player %d already has gamepad id=%u; unassign it first",
                    player,
                    *m_players[static_cast<std::size_t>(player)].assigned_gamepad);
            return false;
        }
        set_gamepad_owner(gamepad_id, previous_player, player);
        return true;
    }

    void input::unassign_gamepad(std::uint32_t gamepad_id)
    {
        const auto it = m_gamepad_owner.find(gamepad_id);
        if (it == m_gamepad_owner.end())
        {
            return;
        }
        const int player = it->second;
        set_gamepad_owner(gamepad_id, player, k_no_player);
        m_gamepad_last_player.erase(gamepad_id);
    }

    int input::gamepad_player(std::uint32_t gamepad_id) const noexcept
    {
        const auto it = m_gamepad_owner.find(gamepad_id);
        return it != m_gamepad_owner.end() ? it->second : k_no_player;
    }

    std::optional<std::uint32_t> input::player_gamepad(int player) const noexcept
    {
        if (!valid_player(player))
        {
            return std::nullopt;
        }
        return m_players[static_cast<std::size_t>(player)].assigned_gamepad;
    }

    const input::gamepad_device_state* input::gamepad_device_for_player(int player) const
    {
        if (!valid_player(player))
        {
            return nullptr;
        }
        const std::optional<std::uint32_t>& assigned = m_players[static_cast<std::size_t>(player)].assigned_gamepad;
        if (!assigned.has_value())
        {
            return nullptr;
        }
        const auto it = m_gamepad_devices.find(*assigned);
        return it != m_gamepad_devices.end() ? &it->second : nullptr;
    }

    input::action_state input::build_action_state(const std::string& name,
                                                  std::initializer_list<action_binding> bindings) const
    {
        action_state state;

        const auto override_it = m_binding_overrides.find(name);
        if (override_it != m_binding_overrides.end())
        {
            std::vector<action_binding> parsed;
            for (const std::string& token : override_it->second)
            {
                if (const auto binding = parse_action_binding(token))
                {
                    parsed.push_back(*binding);
                }
                else
                {
                    LOG_WRN("input: action '%s' binding override '%s' is not recognised; ignoring it",
                            name.c_str(),
                            token.c_str());
                }
            }
            if (!parsed.empty())
            {
                LOG_INF("input: action '%s' rebound from settings (%zu binding(s))", name.c_str(), parsed.size());
                for (const action_binding& binding : parsed)
                {
                    state.bindings.push_back(binding_slot{binding, false});
                }
            }
            else
            {
                LOG_WRN("input: action '%s' has no usable binding override; keeping its compiled default",
                        name.c_str());
                for (const action_binding& binding : bindings)
                {
                    state.bindings.push_back(binding_slot{binding, false});
                }
            }
        }
        else
        {
            for (const action_binding& binding : bindings)
            {
                state.bindings.push_back(binding_slot{binding, false});
            }
        }

        return state;
    }

    input::axis_state input::build_axis_state(const std::string& name,
                                              std::initializer_list<axis_binding> bindings) const
    {
        axis_state state;

        const auto override_it = m_binding_overrides.find(name);
        if (override_it != m_binding_overrides.end())
        {
            std::vector<axis_binding> parsed;
            for (const std::string& token : override_it->second)
            {
                if (const auto binding = parse_axis_binding(token))
                {
                    parsed.push_back(*binding);
                }
                else
                {
                    LOG_WRN("input: axis '%s' binding override '%s' is not recognised; ignoring it",
                            name.c_str(),
                            token.c_str());
                }
            }
            if (!parsed.empty())
            {
                LOG_INF("input: axis '%s' rebound from settings (%zu binding(s))", name.c_str(), parsed.size());
                state.bindings = std::move(parsed);
            }
            else
            {
                LOG_WRN("input: axis '%s' has no usable binding override; keeping its compiled default", name.c_str());
                state.bindings = bindings;
            }
        }
        else
        {
            state.bindings = bindings;
        }

        return state;
    }

    void input::apply_pending_action_rebind(core::string_id id, const std::string& name)
    {
        for (auto& [player, pending] : m_pending_user_bindings)
        {
            const auto it = pending.actions.find(name);
            if (it == pending.actions.end() || !valid_player(player))
            {
                continue;
            }
            std::vector<action_binding> parsed;
            for (const std::string& token : it->second)
            {
                if (const auto binding = parse_action_binding(token))
                {
                    parsed.push_back(*binding);
                }
                else
                {
                    LOG_WRN("input: player %d action '%s' user binding '%s' is not recognised; ignoring it",
                            player,
                            name.c_str(),
                            token.c_str());
                }
            }
            if (parsed.empty())
            {
                LOG_WRN("input: player %d action '%s' has no usable user binding; keeping the default",
                        player,
                        name.c_str());
                continue;
            }
            LOG_INF("input: player %d action '%s' rebound from the user bindings file (%zu binding(s))",
                    player,
                    name.c_str(),
                    parsed.size());
            action_state state;
            for (const action_binding& binding : parsed)
            {
                state.bindings.push_back(binding_slot{binding, false});
            }
            m_players[static_cast<std::size_t>(player)].actions[id] = std::move(state);
            m_players[static_cast<std::size_t>(player)].rebound_actions.insert(id);
        }
    }

    void input::apply_pending_axis_rebind(core::string_id id, const std::string& name)
    {
        for (auto& [player, pending] : m_pending_user_bindings)
        {
            const auto it = pending.axes.find(name);
            if (it == pending.axes.end() || !valid_player(player))
            {
                continue;
            }
            std::vector<axis_binding> parsed;
            for (const std::string& token : it->second)
            {
                if (const auto binding = parse_axis_binding(token))
                {
                    parsed.push_back(*binding);
                }
                else
                {
                    LOG_WRN("input: player %d axis '%s' user binding '%s' is not recognised; ignoring it",
                            player,
                            name.c_str(),
                            token.c_str());
                }
            }
            if (parsed.empty())
            {
                LOG_WRN(
                    "input: player %d axis '%s' has no usable user binding; keeping the default", player, name.c_str());
                continue;
            }
            LOG_INF("input: player %d axis '%s' rebound from the user bindings file (%zu binding(s))",
                    player,
                    name.c_str(),
                    parsed.size());
            axis_state state;
            state.bindings = std::move(parsed);
            m_players[static_cast<std::size_t>(player)].axes[id] = std::move(state);
            m_players[static_cast<std::size_t>(player)].rebound_axes.insert(id);
        }
    }

    void input::bind_action(std::string name, std::initializer_list<action_binding> bindings)
    {
        const core::string_id id{name};
        const action_state state = build_action_state(name, bindings);
        for (player_data& player : m_players)
        {
            player.actions[id] = state;
        }
        apply_pending_action_rebind(id, name);
    }

    void input::bind_axis(std::string name, std::initializer_list<axis_binding> bindings)
    {
        const core::string_id id{name};
        const axis_state state = build_axis_state(name, bindings);
        for (player_data& player : m_players)
        {
            player.axes[id] = state;
        }
        apply_pending_axis_rebind(id, name);
    }

    void input::rebind_action(int player, std::string_view name, std::vector<action_binding> bindings)
    {
        if (!valid_player(player))
        {
            LOG_WRN("input: rebind_action: player %d is out of range (max %d)", player, max_players());
            return;
        }
        const core::string_id id{name};
        action_state state;
        state.bindings.reserve(bindings.size());
        for (const action_binding& binding : bindings)
        {
            state.bindings.push_back(binding_slot{binding, false});
        }
        m_players[static_cast<std::size_t>(player)].actions[id] = std::move(state);
        m_players[static_cast<std::size_t>(player)].rebound_actions.insert(id);
    }

    void input::rebind_axis(int player, std::string_view name, std::vector<axis_binding> bindings)
    {
        if (!valid_player(player))
        {
            LOG_WRN("input: rebind_axis: player %d is out of range (max %d)", player, max_players());
            return;
        }
        const core::string_id id{name};
        axis_state state;
        state.bindings = std::move(bindings);
        m_players[static_cast<std::size_t>(player)].axes[id] = std::move(state);
        m_players[static_cast<std::size_t>(player)].rebound_axes.insert(id);
    }

    void input::push_context(input_context context)
    {
        m_context_stack.push_back(std::move(context));
    }

    void input::pop_context()
    {
        if (m_context_stack.empty())
        {
            LOG_WRN("input: pop_context called with an empty context stack; ignoring it");
            return;
        }
        m_context_stack.pop_back();
    }

    std::string_view input::top_context() const noexcept
    {
        return m_context_stack.empty() ? std::string_view{} : std::string_view{m_context_stack.back().name};
    }

    bool input::is_allowed(std::string_view name, bool axis) const
    {
        for (auto it = m_context_stack.rbegin(); it != m_context_stack.rend(); ++it)
        {
            const std::vector<std::string>& allow_list = axis ? it->allowed_axes : it->allowed_actions;
            if (allow_list.empty() || contains(allow_list, name))
            {
                return true;
            }
            if (it->blocks_lower)
            {
                return false;
            }
        }
        return true;
    }

    bool input::binding_active(int player, const action_binding& binding) const
    {
        switch (binding.source)
        {
        case action_source::key:
            return player == m_keyboard_player && m_keys[static_cast<std::size_t>(binding.key_value)];
        case action_source::mouse_button:
            return player == m_mouse_player && m_mouse_buttons[static_cast<std::size_t>(binding.mouse_value)];
        case action_source::gamepad_button:
        {
            const gamepad_device_state* device = gamepad_device_for_player(player);
            return device != nullptr && device->buttons[static_cast<std::size_t>(binding.gamepad_button_value)];
        }
        case action_source::gamepad_axis:
        {
            const gamepad_device_state* device = gamepad_device_for_player(player);
            return device != nullptr &&
                   crosses_threshold(device->axes[static_cast<std::size_t>(binding.gamepad_axis_value)],
                                     binding.sign,
                                     binding.threshold);
        }
        }
        return false;
    }

    float input::axis_binding_value(int player, const axis_binding& binding) const
    {
        switch (binding.source)
        {
        case axis_source::gamepad_axis:
        {
            const gamepad_device_state* device = gamepad_device_for_player(player);
            if (device == nullptr)
            {
                return 0.0f;
            }
            const float raw = device->axes[static_cast<std::size_t>(binding.gamepad_axis_value)];
            return apply_dead_zone(raw, binding.dead_zone) * binding.scale;
        }
        case axis_source::key_pair:
        {
            if (player != m_keyboard_player)
            {
                return 0.0f;
            }
            float value = 0.0f;
            if (m_keys[static_cast<std::size_t>(binding.positive_key)])
            {
                value += 1.0f;
            }
            if (m_keys[static_cast<std::size_t>(binding.negative_key)])
            {
                value -= 1.0f;
            }
            return value * binding.scale;
        }
        }
        return 0.0f;
    }

    bool input::is_action_down(std::string_view name) const
    {
        return is_action_down(k_default_player, name);
    }

    bool input::is_action_down(int player, std::string_view name) const
    {
        if (!is_allowed(name, false) || !valid_player(player))
        {
            return false;
        }
        const player_data& data = m_players[static_cast<std::size_t>(player)];
        const auto it = data.actions.find(core::string_id{name});
        if (it == data.actions.end())
        {
            return false;
        }
        for (const binding_slot& slot : it->second.bindings)
        {
            if (binding_active(player, slot.value))
            {
                return true;
            }
        }
        return false;
    }

    bool input::was_action_pressed(std::string_view name) const
    {
        return was_action_pressed(k_default_player, name);
    }

    bool input::was_action_pressed(int player, std::string_view name) const
    {
        if (!is_allowed(name, false) || !valid_player(player))
        {
            return false;
        }
        const player_data& data = m_players[static_cast<std::size_t>(player)];
        const auto it = data.actions.find(core::string_id{name});
        return it != data.actions.end() && it->second.pressed_this_step;
    }

    bool input::was_action_released(std::string_view name) const
    {
        return was_action_released(k_default_player, name);
    }

    bool input::was_action_released(int player, std::string_view name) const
    {
        if (!is_allowed(name, false) || !valid_player(player))
        {
            return false;
        }
        const player_data& data = m_players[static_cast<std::size_t>(player)];
        const auto it = data.actions.find(core::string_id{name});
        return it != data.actions.end() && it->second.released_this_step;
    }

    float input::get_axis(std::string_view name) const
    {
        return get_axis(k_default_player, name);
    }

    float input::get_axis(int player, std::string_view name) const
    {
        if (!is_allowed(name, true) || !valid_player(player))
        {
            return 0.0f;
        }
        const player_data& data = m_players[static_cast<std::size_t>(player)];
        const auto it = data.axes.find(core::string_id{name});
        if (it == data.axes.end())
        {
            return 0.0f;
        }
        for (const axis_binding& binding : it->second.bindings)
        {
            const float value = axis_binding_value(player, binding);
            if (value != 0.0f)
            {
                return value;
            }
        }
        return 0.0f;
    }

    core::math::vec2 input::mouse_position() const noexcept
    {
        return m_mouse_position;
    }

    core::math::vec2 input::mouse_delta() const noexcept
    {
        return m_mouse_delta;
    }

    core::math::vec2 input::mouse_wheel_delta() const noexcept
    {
        return m_mouse_wheel_delta;
    }

    void input::listen_for_next_input(int player, std::string_view name, bool is_axis, listen_callback callback)
    {
        if (!valid_player(player))
        {
            LOG_WRN("input: listen_for_next_input: player %d is out of range (max %d)", player, max_players());
            if (callback)
            {
                callback(false);
            }
            return;
        }
        m_listen = listen_state{player, core::string_id{name}, is_axis, std::move(callback)};
        LOG_INF("input: player %d listening for the next input to rebind %s '%.*s'",
                player,
                is_axis ? "axis" : "action",
                static_cast<int>(name.size()),
                name.data());
    }

    bool input::is_listening() const noexcept
    {
        return m_listen.has_value();
    }

    void input::cancel_listen()
    {
        if (!m_listen.has_value())
        {
            return;
        }
        listen_callback callback = std::move(m_listen->callback);
        m_listen.reset();
        if (callback)
        {
            callback(false);
        }
    }

    void input::complete_listen(const action_binding& binding)
    {
        listen_state finished = std::move(*m_listen);
        m_listen.reset();
        rebind_action(finished.player, finished.name.view(), {binding});
        if (finished.callback)
        {
            finished.callback(true);
        }
    }

    void input::complete_listen(const axis_binding& binding)
    {
        listen_state finished = std::move(*m_listen);
        m_listen.reset();
        rebind_axis(finished.player, finished.name.view(), {binding});
        if (finished.callback)
        {
            finished.callback(true);
        }
    }

    void input::try_capture_listen_from_key(key_code code, bool pressed)
    {
        if (!pressed || !m_listen.has_value() || m_listen->is_axis || m_listen->player != m_keyboard_player)
        {
            return;
        }
        complete_listen(action_binding::from_key(code));
    }

    void input::try_capture_listen_from_mouse_button(mouse_key_code code, bool pressed)
    {
        if (!pressed || !m_listen.has_value() || m_listen->is_axis || m_listen->player != m_mouse_player)
        {
            return;
        }
        complete_listen(action_binding::from_mouse_button(code));
    }

    void input::try_capture_listen_from_gamepad_button(std::uint32_t gamepad_id, gamepad_button_code code, bool pressed)
    {
        if (!pressed || !m_listen.has_value() || m_listen->is_axis)
        {
            return;
        }
        const auto owner_it = m_gamepad_owner.find(gamepad_id);
        if (owner_it == m_gamepad_owner.end() || owner_it->second != m_listen->player)
        {
            return;
        }
        complete_listen(action_binding::from_gamepad_button(code));
    }

    void input::try_capture_listen_from_gamepad_axis(std::uint32_t gamepad_id, gamepad_axis_code axis, float value)
    {
        if (!m_listen.has_value())
        {
            return;
        }
        const auto owner_it = m_gamepad_owner.find(gamepad_id);
        if (owner_it == m_gamepad_owner.end() || owner_it->second != m_listen->player)
        {
            return;
        }
        constexpr float k_capture_threshold = 0.5f;
        if (m_listen->is_axis)
        {
            if (std::abs(value) >= k_capture_threshold)
            {
                complete_listen(axis_binding::from_gamepad_axis(axis));
            }
            return;
        }
        if (value >= k_capture_threshold)
        {
            complete_listen(action_binding::from_gamepad_axis(axis, axis_sign::positive));
        }
        else if (value <= -k_capture_threshold)
        {
            complete_listen(action_binding::from_gamepad_axis(axis, axis_sign::negative));
        }
    }

    bool input::load_user_bindings(const std::filesystem::path& path)
    {
        std::string text;
        if (!os::read_text_file(path, text))
        {
            LOG_INF("input: no user bindings file at %s; using the compiled and settings defaults",
                    os::path_to_utf8(path).c_str());
            return false;
        }

        // Exceptions off: a bad document comes back as a discarded value.
        const nlohmann::json document = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
        if (document.is_discarded())
        {
            LOG_WRN("input: %s is not valid JSON; ignoring it", os::path_to_utf8(path).c_str());
            return false;
        }

        if (!document.is_object() || !document.contains("players") || !document["players"].is_object())
        {
            LOG_WRN("input: %s has no 'players' object; ignoring it", os::path_to_utf8(path).c_str());
            return false;
        }

        m_pending_user_bindings.clear();
        std::size_t entries = 0;
        for (const auto& [player_key, player_value] : document["players"].items())
        {
            int player = 0;
            try
            {
                player = std::stoi(player_key);
            }
            catch (const std::exception&)
            {
                LOG_WRN("input: %s has a non-numeric player key '%s'; skipping it",
                        os::path_to_utf8(path).c_str(),
                        player_key.c_str());
                continue;
            }
            if (!player_value.is_object())
            {
                continue;
            }

            pending_player_bindings pending;
            for (const char* section : {"actions", "axes"})
            {
                if (!player_value.contains(section) || !player_value[section].is_object())
                {
                    continue;
                }
                auto& target = std::string_view{section} == "actions" ? pending.actions : pending.axes;
                for (const auto& [name, list] : player_value[section].items())
                {
                    if (!list.is_array())
                    {
                        LOG_WRN("input: %s player %d's '%s' is not an array; skipping it",
                                os::path_to_utf8(path).c_str(),
                                player,
                                name.c_str());
                        continue;
                    }
                    std::vector<std::string> tokens;
                    for (const auto& token : list)
                    {
                        if (token.is_string())
                        {
                            tokens.push_back(token.get<std::string>());
                        }
                    }
                    target[name] = std::move(tokens);
                    ++entries;
                }
            }
            m_pending_user_bindings[player] = std::move(pending);
        }

        LOG_INF("input: loaded %zu user rebind(s) from %s", entries, os::path_to_utf8(path).c_str());
        return true;
    }

    bool input::save_user_bindings(const std::filesystem::path& path) const
    {
        nlohmann::json players = nlohmann::json::object();
        std::size_t entries = 0;

        for (std::size_t player = 0; player < m_players.size(); ++player)
        {
            const player_data& data = m_players[player];

            nlohmann::json actions = nlohmann::json::object();
            for (const core::string_id& name : data.rebound_actions)
            {
                const auto it = data.actions.find(name);
                if (it == data.actions.end())
                {
                    continue;
                }
                nlohmann::json tokens = nlohmann::json::array();
                for (const binding_slot& slot : it->second.bindings)
                {
                    if (const auto text = action_binding_to_string(slot.value))
                    {
                        tokens.push_back(*text);
                    }
                }
                if (!tokens.empty())
                {
                    actions[std::string{name.view()}] = std::move(tokens);
                    ++entries;
                }
            }

            nlohmann::json axes = nlohmann::json::object();
            for (const core::string_id& name : data.rebound_axes)
            {
                const auto it = data.axes.find(name);
                if (it == data.axes.end())
                {
                    continue;
                }
                nlohmann::json tokens = nlohmann::json::array();
                for (const axis_binding& binding : it->second.bindings)
                {
                    if (const auto text = axis_binding_to_string(binding))
                    {
                        tokens.push_back(*text);
                    }
                }
                if (!tokens.empty())
                {
                    axes[std::string{name.view()}] = std::move(tokens);
                    ++entries;
                }
            }

            if (actions.empty() && axes.empty())
            {
                continue;
            }
            nlohmann::json entry = nlohmann::json::object();
            if (!actions.empty())
            {
                entry["actions"] = std::move(actions);
            }
            if (!axes.empty())
            {
                entry["axes"] = std::move(axes);
            }
            players[std::to_string(player)] = std::move(entry);
        }

        const nlohmann::json document = nlohmann::json::object({{"players", std::move(players)}});
        const std::string text = document.dump(2);

        std::error_code error_code;
        std::filesystem::create_directories(path.parent_path(), error_code);

        if (!os::write_file(path, text.data(), text.size()))
        {
            LOG_WRN("input: could not write user bindings to %s", os::path_to_utf8(path).c_str());
            return false;
        }
        LOG_INF("input: saved %zu user rebind(s) to %s", entries, os::path_to_utf8(path).c_str());
        return true;
    }

    void input::begin_step()
    {
        for (player_data& player : m_players)
        {
            for (auto& entry : player.actions)
            {
                action_state& state = entry.second;
                state.pressed_this_step = state.pressed_since_sample;
                state.released_this_step = state.released_since_sample;
                state.pressed_since_sample = false;
                state.released_since_sample = false;
            }
        }
    }

    void input::end_frame()
    {
        m_mouse_delta = m_mouse_delta_accum;
        m_mouse_delta_accum = core::math::vec2{};
        m_mouse_wheel_delta = m_mouse_wheel_delta_accum;
        m_mouse_wheel_delta_accum = core::math::vec2{};
    }

    void input::mark_key_edge(int player, key_code code, bool pressed)
    {
        if (!valid_player(player))
        {
            return;
        }
        for (auto& entry : m_players[static_cast<std::size_t>(player)].actions)
        {
            action_state& state = entry.second;
            for (const binding_slot& slot : state.bindings)
            {
                if (slot.value.source == action_source::key && slot.value.key_value == code)
                {
                    (pressed ? state.pressed_since_sample : state.released_since_sample) = true;
                }
            }
        }
    }

    void input::mark_mouse_edge(int player, mouse_key_code code, bool pressed)
    {
        if (!valid_player(player))
        {
            return;
        }
        for (auto& entry : m_players[static_cast<std::size_t>(player)].actions)
        {
            action_state& state = entry.second;
            for (const binding_slot& slot : state.bindings)
            {
                if (slot.value.source == action_source::mouse_button && slot.value.mouse_value == code)
                {
                    (pressed ? state.pressed_since_sample : state.released_since_sample) = true;
                }
            }
        }
    }

    void input::mark_gamepad_button_edge(int player, gamepad_button_code code, bool pressed)
    {
        if (!valid_player(player))
        {
            return;
        }
        for (auto& entry : m_players[static_cast<std::size_t>(player)].actions)
        {
            action_state& state = entry.second;
            for (const binding_slot& slot : state.bindings)
            {
                if (slot.value.source == action_source::gamepad_button && slot.value.gamepad_button_value == code)
                {
                    (pressed ? state.pressed_since_sample : state.released_since_sample) = true;
                }
            }
        }
    }

    void input::on_key_down(const key_down& event)
    {
        if (event.m_key_code == key_code::unknown)
        {
            return;
        }
        m_keys[static_cast<std::size_t>(event.m_key_code)] = true;
        try_capture_listen_from_key(event.m_key_code, true);
        mark_key_edge(m_keyboard_player, event.m_key_code, true);
    }

    void input::on_key_up(const key_up& event)
    {
        if (event.m_key_code == key_code::unknown)
        {
            return;
        }
        m_keys[static_cast<std::size_t>(event.m_key_code)] = false;
        mark_key_edge(m_keyboard_player, event.m_key_code, false);
    }

    void input::on_mouse_key_down(const mouse_key_down& event)
    {
        m_mouse_buttons[static_cast<std::size_t>(event.m_key_code)] = true;
        m_mouse_position = core::math::vec2{event.m_x, event.m_y};
        try_capture_listen_from_mouse_button(event.m_key_code, true);
        mark_mouse_edge(m_mouse_player, event.m_key_code, true);
    }

    void input::on_mouse_key_up(const mouse_key_up& event)
    {
        m_mouse_buttons[static_cast<std::size_t>(event.m_key_code)] = false;
        m_mouse_position = core::math::vec2{event.m_x, event.m_y};
        mark_mouse_edge(m_mouse_player, event.m_key_code, false);
    }

    void input::on_mouse_move(const mouse_move& event)
    {
        m_mouse_position = core::math::vec2{event.m_x, event.m_y};
        m_mouse_delta_accum += core::math::vec2{event.m_delta_x, event.m_delta_y};
    }

    void input::on_mouse_wheel(const mouse_wheel& event)
    {
        m_mouse_wheel_delta_accum += core::math::vec2{event.m_delta_x, event.m_delta_y};
    }

    void input::on_gamepad_connected(const gamepad_connected& event)
    {
        m_gamepad_devices[event.m_id] = gamepad_device_state{};

        int target = k_no_player;
        const auto remembered = m_gamepad_last_player.find(event.m_id);
        if (remembered != m_gamepad_last_player.end() && valid_player(remembered->second) &&
            !m_players[static_cast<std::size_t>(remembered->second)].assigned_gamepad.has_value())
        {
            target = remembered->second;
        }
        else
        {
            for (int i = 0; i < max_players(); ++i)
            {
                if (!m_players[static_cast<std::size_t>(i)].assigned_gamepad.has_value())
                {
                    target = i;
                    break;
                }
            }
        }

        if (target == k_no_player)
        {
            LOG_WRN("input: gamepad id=%u connected but every player already has one; it drives no player until "
                    "assign_gamepad is called explicitly",
                    event.m_id);
            return;
        }
        set_gamepad_owner(event.m_id, k_no_player, target);
    }

    void input::on_gamepad_disconnected(const gamepad_disconnected& event)
    {
        const auto owner_it = m_gamepad_owner.find(event.m_id);
        if (owner_it != m_gamepad_owner.end())
        {
            const int player = owner_it->second;

            // The device's buttons are still readable here (erased below), so let go of anything this player
            // was holding down through it — no more of its events are coming to release them naturally.
            const auto device_it = m_gamepad_devices.find(event.m_id);
            if (device_it != m_gamepad_devices.end())
            {
                for (std::size_t i = 0; i < device_it->second.buttons.size(); ++i)
                {
                    if (device_it->second.buttons[i])
                    {
                        mark_gamepad_button_edge(player, static_cast<gamepad_button_code>(i), false);
                    }
                }
            }
            for (auto& entry : m_players[static_cast<std::size_t>(player)].actions)
            {
                action_state& state = entry.second;
                for (binding_slot& slot : state.bindings)
                {
                    if (slot.value.source == action_source::gamepad_axis && slot.axis_was_active)
                    {
                        slot.axis_was_active = false;
                        state.released_since_sample = true;
                    }
                }
            }

            set_gamepad_owner(event.m_id, player, k_no_player);
        }
        m_gamepad_devices.erase(event.m_id);
    }

    void input::on_gamepad_button(const gamepad_button& event)
    {
        if (event.m_button == gamepad_button_code::unknown)
        {
            return;
        }
        const auto device_it = m_gamepad_devices.find(event.m_id);
        if (device_it == m_gamepad_devices.end())
        {
            return;
        }
        device_it->second.buttons[static_cast<std::size_t>(event.m_button)] = event.m_pressed;

        try_capture_listen_from_gamepad_button(event.m_id, event.m_button, event.m_pressed);

        const auto owner_it = m_gamepad_owner.find(event.m_id);
        if (owner_it != m_gamepad_owner.end())
        {
            mark_gamepad_button_edge(owner_it->second, event.m_button, event.m_pressed);
        }
    }

    void input::on_gamepad_axis(const gamepad_axis& event)
    {
        if (event.m_axis == gamepad_axis_code::unknown)
        {
            return;
        }
        const auto device_it = m_gamepad_devices.find(event.m_id);
        if (device_it == m_gamepad_devices.end())
        {
            return;
        }
        device_it->second.axes[static_cast<std::size_t>(event.m_axis)] = event.m_value;

        try_capture_listen_from_gamepad_axis(event.m_id, event.m_axis, event.m_value);

        const auto owner_it = m_gamepad_owner.find(event.m_id);
        if (owner_it == m_gamepad_owner.end())
        {
            return;
        }
        const int player = owner_it->second;
        for (auto& entry : m_players[static_cast<std::size_t>(player)].actions)
        {
            action_state& state = entry.second;
            for (binding_slot& slot : state.bindings)
            {
                if (slot.value.source != action_source::gamepad_axis || slot.value.gamepad_axis_value != event.m_axis)
                {
                    continue;
                }
                const bool now_active = crosses_threshold(event.m_value, slot.value.sign, slot.value.threshold);
                if (now_active && !slot.axis_was_active)
                {
                    state.pressed_since_sample = true;
                }
                if (!now_active && slot.axis_was_active)
                {
                    state.released_since_sample = true;
                }
                slot.axis_was_active = now_active;
            }
        }
    }

    void register_settings(settings_registry& registry, input_settings& out)
    {
        typed_section<input_settings> section = add_typed_section(registry, "input", out);

        section.add_number("mouse_sensitivity", &input_settings::mouse_sensitivity, 0.0001f, 10.0f);
        section.add_flag("mouse_reversed", &input_settings::mouse_reversed);
        section.add_count("max_players", &input_settings::max_players, 1, 8)
            .with_env("ALPHAENGINE_INPUT_MAX_PLAYERS")
            .with_cli("--input-max-players")
            .with_help("  --input-max-players <n>  local player slots core::input maintains (default 4)\n");

        // input.bindings: { "<action or axis name>": ["<binding string>", ...], ... }. The binding-string
        // grammar itself is core::input's concern (core/input.cpp's bind_action / bind_axis rebind path);
        // this only captures the raw strings, tolerant the same way as every scalar field, so a bad entry is
        // warned about and skipped rather than dropping the whole section.
        section.set_custom_json(
            [](input_settings& out_settings, std::string_view key, const nlohmann::json& value)
            {
                if (key != "bindings")
                {
                    LOG_WRN("settings: ignoring unknown key 'input.%s'", std::string{key}.c_str());
                    return;
                }
                if (!value.is_object())
                {
                    LOG_WRN("settings: input.bindings must be a JSON object; ignoring it");
                    return;
                }
                for (const auto& [name, list] : value.items())
                {
                    if (!list.is_array())
                    {
                        LOG_WRN("settings: input.bindings.%s must be an array of strings; ignoring it", name.c_str());
                        continue;
                    }
                    std::vector<std::string> parsed;
                    for (const auto& entry : list)
                    {
                        if (!entry.is_string())
                        {
                            LOG_WRN("settings: input.bindings.%s has a non-string entry; skipping it", name.c_str());
                            continue;
                        }
                        parsed.push_back(entry.get<std::string>());
                    }
                    out_settings.bindings[name] = std::move(parsed);
                }
            });

        section.set_log_resolved(
            [](const input_settings& s)
            {
                LOG_INF("Input settings resolved: mouse_sensitivity=%.4f mouse_reversed=%s max_players=%u",
                        static_cast<double>(s.mouse_sensitivity),
                        on_off(s.mouse_reversed),
                        s.max_players);
            });
    }
} // namespace core
