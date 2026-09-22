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

/**
 * @file settings_parse.hpp
 * @brief The pure, device-free half of settings resolution: the value parsers and the per-layer @c apply_*
 *        steps (a JSON document, an environment lookup, a command line). @ref core::load_settings strings
 *        them together against the real process; tests feed them strings and arrays directly.
 *
 * Every parser is lenient in the same way: input is trimmed, names are matched ignoring case, and an
 * unrecognised or out-of-range value is reported with @c LOG_WRN and leaves the setting unchanged, so bad
 * configuration never stops the engine from starting.
 */

#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <core/settings.hpp>

namespace core
{
    /** @brief Largest window dimension accepted from any source; 0 means "match the display". */
    inline constexpr unsigned int k_max_window_dimension = 16384;
    inline constexpr float k_min_field_of_view = 1.0f;
    inline constexpr float k_max_field_of_view = 179.0f;
    inline constexpr float k_min_mouse_sensitivity = 0.0001f;
    inline constexpr float k_max_mouse_sensitivity = 10.0f;

    /** @brief Parses `true` / `false`, `1` / `0`, `yes` / `no`, `on` / `off`; trimmed, case-insensitive. */
    std::optional<bool> parse_bool(std::string_view text);

    /** @brief Parses a decimal unsigned integer in `[min, max]`; a sign or trailing characters reject it. */
    std::optional<unsigned int> parse_unsigned(std::string_view text, unsigned int min, unsigned int max);

    /** @brief Parses a finite decimal number in `[min, max]` in the C locale; trailing characters reject it. */
    std::optional<float> parse_float(std::string_view text, float min, float max);

    /** @brief Parses `windowed`, `fullscreen` or `borderless`; trimmed, case-insensitive. */
    std::optional<window_mode> parse_window_mode(std::string_view text);

    /** @brief Parses `opengl` or `vulkan`; trimmed, case-insensitive. */
    std::optional<graphics_backend> parse_graphics_backend(std::string_view text);

    /**
     * @brief Applies a `settings.json` document (schema in docs/settings.md) on top of @p out. Only the keys
     *        present are applied; an unknown key, a value of the wrong JSON type or one outside its range is
     *        warned about and skipped.
     * @return false when @p text is not a JSON object at all (nothing was applied), true otherwise.
     */
    bool apply_json(settings& out, std::string_view text);

    /** @brief Environment lookup: the value of the named variable, or null when it is unset. */
    using environment_getter = std::function<const char*(const char* name)>;

    /**
     * @brief Applies the `ALPHAENGINE_WIDTH`, `_HEIGHT`, `_WINDOW_MODE`, `_VSYNC`, `_GRAPHICS_BACKEND` and
     *        `_TAA` variables on top of @p out. An unset or empty variable leaves its setting as it is.
     *        (`ALPHAENGINE_LOG_LEVEL` belongs to @ref core::logging::init.)
     */
    void apply_environment(settings& out, const environment_getter& get);

    /** @brief What @ref parse_command_line recognised; an empty optional was not on the command line. */
    struct command_line_options
    {
        std::optional<unsigned int> width;
        std::optional<unsigned int> height;
        std::optional<window_mode> mode;
        std::optional<graphics_backend> backend;
        std::optional<bool> vsync;

        /** @brief The `--log-level` specification, for @ref core::logging::configure_levels. */
        std::optional<std::string> log_level;

        /** @brief The `--settings` override of the settings-file path. */
        std::optional<std::string> settings_path;

        bool help_requested{false};
    };

    /**
     * @brief Parses the command line, given without the program name. Both `--key value` and `--key=value`
     *        are accepted; see @ref command_line_usage for the options. An unknown option, a missing value or
     *        an unparsable value is warned about and skipped, so parsing never fails; when an option repeats,
     *        the last valid occurrence wins.
     */
    command_line_options parse_command_line(std::span<const char* const> args);

    /** @brief Applies the window / graphics options present in @p options on top of @p out. */
    void apply_command_line(settings& out, const command_line_options& options);

    /** @brief The `--help` text. */
    const char* command_line_usage() noexcept;
} // namespace core
