/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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

#include <core/settings_parse.hpp>

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include <nlohmann/json.hpp>

#include <core/log.hpp>

namespace core
{
    namespace
    {
        using json = nlohmann::json;

        std::string_view trim(std::string_view text)
        {
            const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
            while (!text.empty() && is_space(text.front()))
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && is_space(text.back()))
            {
                text.remove_suffix(1);
            }
            return text;
        }

        bool equals_ignoring_case(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                {
                    return false;
                }
            }
            return true;
        }

        template<typename T>
        void assign_if(T& target, const std::optional<T>& value)
        {
            if (value.has_value())
            {
                target = *value;
            }
        }

        // Keeps a previously parsed value when a later occurrence fails to parse.
        template<typename T>
        void store_if(std::optional<T>& target, const std::optional<T>& value)
        {
            if (value.has_value())
            {
                target = value;
            }
        }

        // -- Text parsers with the warning attached ---------------------------
        //
        // Shared by the environment and command-line layers; `source` names
        // the variable or option in the message.

        std::optional<unsigned int>
        parse_unsigned_or_warn(const char* source, std::string_view text, unsigned int min, unsigned int max)
        {
            const auto parsed = parse_unsigned(text, min, max);
            if (!parsed.has_value())
            {
                LOG_WRN("settings: %s='%s' is not an integer in [%u, %u]; ignoring it",
                        source,
                        std::string{text}.c_str(),
                        min,
                        max);
            }
            return parsed;
        }

        std::optional<bool> parse_bool_or_warn(const char* source, std::string_view text)
        {
            const auto parsed = parse_bool(text);
            if (!parsed.has_value())
            {
                LOG_WRN("settings: %s='%s' is not a boolean (true/false, 1/0, yes/no, on/off); ignoring it",
                        source,
                        std::string{text}.c_str());
            }
            return parsed;
        }

        std::optional<window_mode> parse_window_mode_or_warn(const char* source, std::string_view text)
        {
            const auto parsed = parse_window_mode(text);
            if (!parsed.has_value())
            {
                LOG_WRN("settings: %s='%s' is not one of windowed|fullscreen|borderless; ignoring it",
                        source,
                        std::string{text}.c_str());
            }
            return parsed;
        }

        std::optional<graphics_backend> parse_graphics_backend_or_warn(const char* source, std::string_view text)
        {
            const auto parsed = parse_graphics_backend(text);
            if (!parsed.has_value())
            {
                LOG_WRN(
                    "settings: %s='%s' is not one of opengl|vulkan; ignoring it", source, std::string{text}.c_str());
            }
            return parsed;
        }

        // -- JSON -------------------------------------------------------------
        //
        // One setter per field type. Each checks the JSON type and the range
        // and warns with the dotted key on a mismatch, leaving the target as
        // it was.

        void set_from_json(unsigned int& target, const char* key, const json& value, unsigned int min, unsigned int max)
        {
            if (!value.is_number_unsigned())
            {
                LOG_WRN("settings: %s must be an unsigned integer; keeping %u", key, target);
                return;
            }
            const auto raw = value.get<std::uint64_t>();
            if (raw < min || raw > max)
            {
                LOG_WRN("settings: %s=%llu is outside [%u, %u]; keeping %u",
                        key,
                        static_cast<unsigned long long>(raw),
                        min,
                        max,
                        target);
                return;
            }
            target = static_cast<unsigned int>(raw);
        }

        void set_from_json(float& target, const char* key, const json& value, float min, float max)
        {
            if (!value.is_number())
            {
                LOG_WRN("settings: %s must be a number; keeping %g", key, static_cast<double>(target));
                return;
            }
            const double raw = value.get<double>();
            if (!std::isfinite(raw) || raw < static_cast<double>(min) || raw > static_cast<double>(max))
            {
                LOG_WRN("settings: %s=%g is outside [%g, %g]; keeping %g",
                        key,
                        raw,
                        static_cast<double>(min),
                        static_cast<double>(max),
                        static_cast<double>(target));
                return;
            }
            target = static_cast<float>(raw);
        }

        void set_from_json(bool& target, const char* key, const json& value)
        {
            if (!value.is_boolean())
            {
                LOG_WRN("settings: %s must be true or false; keeping %s", key, target ? "true" : "false");
                return;
            }
            target = value.get<bool>();
        }

        void set_from_json(std::string& target, const char* key, const json& value)
        {
            if (!value.is_string())
            {
                LOG_WRN("settings: %s must be a string; keeping '%s'", key, target.c_str());
                return;
            }
            target = value.get<std::string>();
        }

        void set_from_json(window_mode& target, const char* key, const json& value)
        {
            if (!value.is_string())
            {
                LOG_WRN("settings: %s must be a string; keeping %s", key, window_mode_name(target));
                return;
            }
            const auto& text = value.get_ref<const std::string&>();
            const auto parsed = parse_window_mode(text);
            if (!parsed.has_value())
            {
                LOG_WRN("settings: %s='%s' is not one of windowed|fullscreen|borderless; keeping %s",
                        key,
                        text.c_str(),
                        window_mode_name(target));
                return;
            }
            target = *parsed;
        }

        void set_from_json(graphics_backend& target, const char* key, const json& value)
        {
            if (!value.is_string())
            {
                LOG_WRN("settings: %s must be a string; keeping %s", key, graphics_backend_name(target));
                return;
            }
            const auto& text = value.get_ref<const std::string&>();
            const auto parsed = parse_graphics_backend(text);
            if (!parsed.has_value())
            {
                LOG_WRN("settings: %s='%s' is not one of opengl|vulkan; keeping %s",
                        key,
                        text.c_str(),
                        graphics_backend_name(target));
                return;
            }
            target = *parsed;
        }

        void warn_unknown_key(const char* section, const std::string& key)
        {
            LOG_WRN("settings: ignoring unknown key '%s.%s'", section, key.c_str());
        }

        void apply_window_section(settings& out, const json& section)
        {
            for (const auto& [key, value] : section.items())
            {
                if (key == "width")
                {
                    set_from_json(out.window.width, "window.width", value, 0, k_max_window_dimension);
                }
                else if (key == "height")
                {
                    set_from_json(out.window.height, "window.height", value, 0, k_max_window_dimension);
                }
                else if (key == "mode")
                {
                    set_from_json(out.window.mode, "window.mode", value);
                }
                else if (key == "vsync")
                {
                    set_from_json(out.window.vsync, "window.vsync", value);
                }
                else if (key == "title")
                {
                    set_from_json(out.window.title, "window.title", value);
                }
                else
                {
                    warn_unknown_key("window", key);
                }
            }
        }

        void apply_graphics_section(settings& out, const json& section)
        {
            for (const auto& [key, value] : section.items())
            {
                if (key == "backend")
                {
                    set_from_json(out.graphics.backend, "graphics.backend", value);
                }
                else if (key == "temporal_aa")
                {
                    set_from_json(out.graphics.temporal_aa, "graphics.temporal_aa", value);
                }
                else
                {
                    warn_unknown_key("graphics", key);
                }
            }
        }

        void apply_camera_section(settings& out, const json& section)
        {
            for (const auto& [key, value] : section.items())
            {
                if (key == "fov_degrees")
                {
                    set_from_json(out.camera.field_of_view,
                                  "camera.fov_degrees",
                                  value,
                                  k_min_field_of_view,
                                  k_max_field_of_view);
                }
                else
                {
                    warn_unknown_key("camera", key);
                }
            }
        }

        void apply_input_section(settings& out, const json& section)
        {
            for (const auto& [key, value] : section.items())
            {
                if (key == "mouse_sensitivity")
                {
                    set_from_json(out.input.mouse_sensitivity,
                                  "input.mouse_sensitivity",
                                  value,
                                  k_min_mouse_sensitivity,
                                  k_max_mouse_sensitivity);
                }
                else if (key == "mouse_reversed")
                {
                    set_from_json(out.input.mouse_reversed, "input.mouse_reversed", value);
                }
                else
                {
                    warn_unknown_key("input", key);
                }
            }
        }

        // -- Command line -----------------------------------------------------

        enum class value_option
        {
            width,
            height,
            backend,
            vsync,
            log_level,
            settings_path
        };

        struct value_option_entry
        {
            std::string_view name;
            value_option kind;
        };

        constexpr value_option_entry k_value_options[] = {
            {"--width", value_option::width},
            {"--height", value_option::height},
            {"--backend", value_option::backend},
            {"--vsync", value_option::vsync},
            {"--log-level", value_option::log_level},
            {"--settings", value_option::settings_path},
        };

        struct mode_flag_entry
        {
            std::string_view name;
            window_mode mode;
        };

        constexpr mode_flag_entry k_mode_flags[] = {
            {"--windowed", window_mode::windowed},
            {"--fullscreen", window_mode::fullscreen},
            {"--borderless", window_mode::borderless},
        };

        std::optional<value_option> find_value_option(std::string_view name)
        {
            for (const auto& entry : k_value_options)
            {
                if (entry.name == name)
                {
                    return entry.kind;
                }
            }
            return std::nullopt;
        }

        std::optional<window_mode> find_mode_flag(std::string_view name)
        {
            for (const auto& entry : k_mode_flags)
            {
                if (entry.name == name)
                {
                    return entry.mode;
                }
            }
            return std::nullopt;
        }

        bool looks_like_option(const char* arg)
        {
            return arg != nullptr && std::string_view{arg}.starts_with("--");
        }

        constexpr const char k_usage[] = R"(Usage: AlphaEngine [options]

Options:
  --width <n>          window width in points (0 = match the display)
  --height <n>         window height in points (0 = match the display)
  --windowed           decorated window
  --fullscreen         fullscreen at the display's resolution
  --borderless         borderless window
  --backend <name>     gpu backend: opengl or vulkan
  --vsync <on|off>     wait for vertical sync
  --log-level <spec>   log level, e.g. warn or info,gpu=trace
  --settings <path>    settings file to read instead of the default
  -h, --help           print this text and exit

Every option also accepts the --key=value form. Command-line values override
the ALPHAENGINE_* environment variables, which override the settings file
(see docs/settings.md).
)";
    } // namespace

    std::optional<bool> parse_bool(std::string_view text)
    {
        text = trim(text);
        constexpr std::string_view k_true_words[] = {"true", "1", "yes", "on"};
        constexpr std::string_view k_false_words[] = {"false", "0", "no", "off"};
        for (const std::string_view word : k_true_words)
        {
            if (equals_ignoring_case(text, word))
            {
                return true;
            }
        }
        for (const std::string_view word : k_false_words)
        {
            if (equals_ignoring_case(text, word))
            {
                return false;
            }
        }
        return std::nullopt;
    }

    std::optional<unsigned int> parse_unsigned(std::string_view text, unsigned int min, unsigned int max)
    {
        text = trim(text);
        if (text.empty())
        {
            return std::nullopt;
        }
        unsigned int value = 0;
        const char* const end = text.data() + text.size();
        // from_chars rejects a leading sign for unsigned targets, and the
        // pointer check rejects trailing characters.
        const auto [ptr, ec] = std::from_chars(text.data(), end, value, 10);
        if (ec != std::errc{} || ptr != end)
        {
            return std::nullopt;
        }
        if (value < min || value > max)
        {
            return std::nullopt;
        }
        return value;
    }

    std::optional<float> parse_float(std::string_view text, float min, float max)
    {
        text = trim(text);
        if (text.empty())
        {
            return std::nullopt;
        }
        // A classic-locale stream rather than strtof so a decimal-comma
        // process locale cannot change what "0.5" means, and rather than
        // from_chars because not every supported standard library ships the
        // floating-point overloads yet.
        std::istringstream stream{std::string{text}};
        stream.imbue(std::locale::classic());
        double value = 0.0;
        stream >> value;
        if (stream.fail() || stream.peek() != std::char_traits<char>::eof())
        {
            return std::nullopt;
        }
        if (!std::isfinite(value) || value < static_cast<double>(min) || value > static_cast<double>(max))
        {
            return std::nullopt;
        }
        return static_cast<float>(value);
    }

    std::optional<window_mode> parse_window_mode(std::string_view text)
    {
        text = trim(text);
        constexpr window_mode k_all_modes[] = {window_mode::windowed, window_mode::fullscreen, window_mode::borderless};
        for (const window_mode mode : k_all_modes)
        {
            if (equals_ignoring_case(text, window_mode_name(mode)))
            {
                return mode;
            }
        }
        return std::nullopt;
    }

    std::optional<graphics_backend> parse_graphics_backend(std::string_view text)
    {
        text = trim(text);
        constexpr graphics_backend k_all_backends[] = {graphics_backend::opengl, graphics_backend::vulkan};
        for (const graphics_backend backend : k_all_backends)
        {
            if (equals_ignoring_case(text, graphics_backend_name(backend)))
            {
                return backend;
            }
        }
        return std::nullopt;
    }

    bool apply_json(settings& out, std::string_view text)
    {
        // Exceptions off: a bad document comes back as a discarded value.
        const json document = json::parse(text.begin(), text.end(), nullptr, false);
        if (document.is_discarded())
        {
            LOG_WRN("settings: the settings file is not valid JSON; ignoring it");
            return false;
        }
        if (!document.is_object())
        {
            LOG_WRN("settings: the settings file must hold a JSON object; ignoring it");
            return false;
        }

        for (const auto& [name, section] : document.items())
        {
            if (!section.is_object())
            {
                LOG_WRN("settings: '%s' must be a JSON object; ignoring it", name.c_str());
                continue;
            }
            if (name == "window")
            {
                apply_window_section(out, section);
            }
            else if (name == "graphics")
            {
                apply_graphics_section(out, section);
            }
            else if (name == "camera")
            {
                apply_camera_section(out, section);
            }
            else if (name == "input")
            {
                apply_input_section(out, section);
            }
            else
            {
                LOG_WRN("settings: ignoring unknown section '%s'", name.c_str());
            }
        }
        return true;
    }

    void apply_environment(settings& out, const environment_getter& get)
    {
        // An unset or blank variable is "not configured", never a warning.
        const auto read = [&get](const char* name) -> std::optional<std::string_view>
        {
            const char* value = get ? get(name) : nullptr;
            if (value == nullptr || trim(value).empty())
            {
                return std::nullopt;
            }
            return std::string_view{value};
        };

        if (const auto text = read("ALPHAENGINE_WIDTH"))
        {
            assign_if(out.window.width, parse_unsigned_or_warn("ALPHAENGINE_WIDTH", *text, 0, k_max_window_dimension));
        }
        if (const auto text = read("ALPHAENGINE_HEIGHT"))
        {
            assign_if(out.window.height,
                      parse_unsigned_or_warn("ALPHAENGINE_HEIGHT", *text, 0, k_max_window_dimension));
        }
        if (const auto text = read("ALPHAENGINE_WINDOW_MODE"))
        {
            assign_if(out.window.mode, parse_window_mode_or_warn("ALPHAENGINE_WINDOW_MODE", *text));
        }
        if (const auto text = read("ALPHAENGINE_VSYNC"))
        {
            assign_if(out.window.vsync, parse_bool_or_warn("ALPHAENGINE_VSYNC", *text));
        }
        if (const auto text = read("ALPHAENGINE_GRAPHICS_BACKEND"))
        {
            assign_if(out.graphics.backend, parse_graphics_backend_or_warn("ALPHAENGINE_GRAPHICS_BACKEND", *text));
        }
        if (const auto text = read("ALPHAENGINE_TAA"))
        {
            assign_if(out.graphics.temporal_aa, parse_bool_or_warn("ALPHAENGINE_TAA", *text));
        }
    }

    command_line_options parse_command_line(std::span<const char* const> args)
    {
        command_line_options out;
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            if (args[i] == nullptr)
            {
                continue;
            }
            const std::string_view arg{args[i]};
            if (arg == "--help" || arg == "-h")
            {
                out.help_requested = true;
                continue;
            }
            if (!arg.starts_with("--") || arg.size() == 2)
            {
                LOG_WRN("settings: ignoring unexpected argument '%s'", std::string{arg}.c_str());
                continue;
            }

            // --key=value splits here; --key value picks its value up below.
            const std::size_t equals = arg.find('=');
            const std::string name{arg.substr(0, equals)};
            std::optional<std::string_view> value;
            if (equals != std::string_view::npos)
            {
                value = arg.substr(equals + 1);
            }

            if (const auto mode = find_mode_flag(name); mode.has_value())
            {
                if (value.has_value())
                {
                    LOG_WRN("settings: %s takes no value; ignoring it", name.c_str());
                    continue;
                }
                out.mode = *mode;
                continue;
            }

            const auto option = find_value_option(name);
            if (!option.has_value())
            {
                LOG_WRN("settings: ignoring unknown option '%s'", name.c_str());
                continue;
            }
            if (!value.has_value() && i + 1 < args.size() && args[i + 1] != nullptr && !looks_like_option(args[i + 1]))
            {
                ++i;
                value = std::string_view{args[i]};
            }
            if (!value.has_value() || trim(*value).empty())
            {
                LOG_WRN("settings: %s needs a value; ignoring it", name.c_str());
                continue;
            }

            switch (*option)
            {
            case value_option::width:
                store_if(out.width, parse_unsigned_or_warn(name.c_str(), *value, 0, k_max_window_dimension));
                break;
            case value_option::height:
                store_if(out.height, parse_unsigned_or_warn(name.c_str(), *value, 0, k_max_window_dimension));
                break;
            case value_option::backend:
                store_if(out.backend, parse_graphics_backend_or_warn(name.c_str(), *value));
                break;
            case value_option::vsync:
                store_if(out.vsync, parse_bool_or_warn(name.c_str(), *value));
                break;
            case value_option::log_level:
                out.log_level = std::string{trim(*value)};
                break;
            case value_option::settings_path:
                out.settings_path = std::string{trim(*value)};
                break;
            }
        }
        return out;
    }

    void apply_command_line(settings& out, const command_line_options& options)
    {
        assign_if(out.window.width, options.width);
        assign_if(out.window.height, options.height);
        assign_if(out.window.mode, options.mode);
        assign_if(out.window.vsync, options.vsync);
        assign_if(out.graphics.backend, options.backend);
    }

    const char* command_line_usage() noexcept
    {
        return k_usage;
    }
} // namespace core
