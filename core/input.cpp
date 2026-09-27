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

#include <core/input.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <utility>

#include <core/event_engine.hpp>
#include <core/log.hpp>
#include <core/settings.hpp>

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
        // Every name matches the corresponding enumerator's own spelling
        // (case-insensitively), so a settings.json author can read the enum
        // in core/event.hpp to know what is bindable.

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

            static const std::pair<std::string_view, key_code> k_named_keys[] = {
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
            static const std::pair<std::string_view, mouse_key_code> k_named_buttons[] = {
                {"left", mouse_key_code::left},
                {"right", mouse_key_code::right},
                {"middle", mouse_key_code::middle},
                {"x1", mouse_key_code::x1},
                {"x2", mouse_key_code::x2},
            };
            for (const auto& [button_name, code] : k_named_buttons)
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
            static const std::pair<std::string_view, gamepad_button_code> k_named_buttons[] = {
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
            for (const auto& [button_name, code] : k_named_buttons)
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
            static const std::pair<std::string_view, gamepad_axis_code> k_named_axes[] = {
                {"left_x", gamepad_axis_code::left_x},
                {"left_y", gamepad_axis_code::left_y},
                {"right_x", gamepad_axis_code::right_x},
                {"right_y", gamepad_axis_code::right_y},
                {"left_trigger", gamepad_axis_code::left_trigger},
                {"right_trigger", gamepad_axis_code::right_trigger},
            };
            for (const auto& [axis_name, code] : k_named_axes)
            {
                if (axis_name == name)
                {
                    return code;
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
        m_binding_overrides = settings.bindings;

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

        LOG_INF("Init core::input (%zu binding override(s) from settings)", m_binding_overrides.size());
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

        m_actions.clear();
        m_axes.clear();
        m_binding_overrides.clear();
        m_context_stack.clear();

        m_keys.fill(false);
        m_mouse_buttons.fill(false);
        m_mouse_position = core::math::vec2{};
        m_mouse_delta_accum = core::math::vec2{};
        m_mouse_delta = core::math::vec2{};
        m_mouse_wheel_delta_accum = core::math::vec2{};
        m_mouse_wheel_delta = core::math::vec2{};
        m_gamepad = gamepad_state{};
        m_connected_gamepads = 0;

        LOG_INF("Quit core::input");
    }

    void input::bind_action(std::string name, std::initializer_list<action_binding> bindings)
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

        m_actions[std::move(name)] = std::move(state);
    }

    void input::bind_axis(std::string name, std::initializer_list<axis_binding> bindings)
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

        m_axes[std::move(name)] = std::move(state);
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

    bool input::binding_active(const action_binding& binding) const
    {
        switch (binding.source)
        {
        case action_source::key:
            return m_keys[static_cast<std::size_t>(binding.key_value)];
        case action_source::mouse_button:
            return m_mouse_buttons[static_cast<std::size_t>(binding.mouse_value)];
        case action_source::gamepad_button:
            return m_gamepad.buttons[static_cast<std::size_t>(binding.gamepad_button_value)];
        case action_source::gamepad_axis:
            return crosses_threshold(
                m_gamepad.axes[static_cast<std::size_t>(binding.gamepad_axis_value)], binding.sign, binding.threshold);
        }
        return false;
    }

    float input::axis_binding_value(const axis_binding& binding) const
    {
        switch (binding.source)
        {
        case axis_source::gamepad_axis:
        {
            const float raw = m_gamepad.axes[static_cast<std::size_t>(binding.gamepad_axis_value)];
            return apply_dead_zone(raw, binding.dead_zone) * binding.scale;
        }
        case axis_source::key_pair:
        {
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
        if (!is_allowed(name, false))
        {
            return false;
        }
        const auto it = m_actions.find(std::string{name});
        if (it == m_actions.end())
        {
            return false;
        }
        for (const binding_slot& slot : it->second.bindings)
        {
            if (binding_active(slot.value))
            {
                return true;
            }
        }
        return false;
    }

    bool input::was_action_pressed(std::string_view name) const
    {
        if (!is_allowed(name, false))
        {
            return false;
        }
        const auto it = m_actions.find(std::string{name});
        return it != m_actions.end() && it->second.pressed_this_step;
    }

    bool input::was_action_released(std::string_view name) const
    {
        if (!is_allowed(name, false))
        {
            return false;
        }
        const auto it = m_actions.find(std::string{name});
        return it != m_actions.end() && it->second.released_this_step;
    }

    float input::get_axis(std::string_view name) const
    {
        if (!is_allowed(name, true))
        {
            return 0.0f;
        }
        const auto it = m_axes.find(std::string{name});
        if (it == m_axes.end())
        {
            return 0.0f;
        }
        for (const axis_binding& binding : it->second.bindings)
        {
            const float value = axis_binding_value(binding);
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

    void input::begin_step()
    {
        for (auto& entry : m_actions)
        {
            action_state& state = entry.second;
            state.pressed_this_step = state.pressed_since_sample;
            state.released_this_step = state.released_since_sample;
            state.pressed_since_sample = false;
            state.released_since_sample = false;
        }
    }

    void input::end_frame()
    {
        m_mouse_delta = m_mouse_delta_accum;
        m_mouse_delta_accum = core::math::vec2{};
        m_mouse_wheel_delta = m_mouse_wheel_delta_accum;
        m_mouse_wheel_delta_accum = core::math::vec2{};
    }

    void input::mark_key_edge(key_code code, bool pressed)
    {
        for (auto& entry : m_actions)
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

    void input::mark_mouse_edge(mouse_key_code code, bool pressed)
    {
        for (auto& entry : m_actions)
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

    void input::mark_gamepad_button_edge(gamepad_button_code code, bool pressed)
    {
        for (auto& entry : m_actions)
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
        mark_key_edge(event.m_key_code, true);
    }

    void input::on_key_up(const key_up& event)
    {
        if (event.m_key_code == key_code::unknown)
        {
            return;
        }
        m_keys[static_cast<std::size_t>(event.m_key_code)] = false;
        mark_key_edge(event.m_key_code, false);
    }

    void input::on_mouse_key_down(const mouse_key_down& event)
    {
        m_mouse_buttons[static_cast<std::size_t>(event.m_key_code)] = true;
        m_mouse_position = core::math::vec2{event.m_x, event.m_y};
        mark_mouse_edge(event.m_key_code, true);
    }

    void input::on_mouse_key_up(const mouse_key_up& event)
    {
        m_mouse_buttons[static_cast<std::size_t>(event.m_key_code)] = false;
        m_mouse_position = core::math::vec2{event.m_x, event.m_y};
        mark_mouse_edge(event.m_key_code, false);
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

    void input::on_gamepad_connected(const gamepad_connected&)
    {
        ++m_connected_gamepads;
    }

    void input::on_gamepad_disconnected(const gamepad_disconnected&)
    {
        if (m_connected_gamepads > 0)
        {
            --m_connected_gamepads;
        }
        if (m_connected_gamepads != 0)
        {
            return;
        }

        // The last gamepad went away: let go of everything it might have left held rather than leave an action
        // or axis stuck, since no more of its events are coming to release them naturally.
        for (std::size_t i = 0; i < m_gamepad.buttons.size(); ++i)
        {
            if (m_gamepad.buttons[i])
            {
                m_gamepad.buttons[i] = false;
                mark_gamepad_button_edge(static_cast<gamepad_button_code>(i), false);
            }
        }
        for (auto& entry : m_actions)
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
        m_gamepad.axes.fill(0.0f);
    }

    void input::on_gamepad_button(const gamepad_button& event)
    {
        if (event.m_button == gamepad_button_code::unknown)
        {
            return;
        }
        m_gamepad.buttons[static_cast<std::size_t>(event.m_button)] = event.m_pressed;
        mark_gamepad_button_edge(event.m_button, event.m_pressed);
    }

    void input::on_gamepad_axis(const gamepad_axis& event)
    {
        if (event.m_axis == gamepad_axis_code::unknown)
        {
            return;
        }
        m_gamepad.axes[static_cast<std::size_t>(event.m_axis)] = event.m_value;

        for (auto& entry : m_actions)
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
} // namespace core
