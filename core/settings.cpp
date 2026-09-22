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

#include <core/settings.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include <core/log.hpp>
#include <core/settings_parse.hpp>
#include <core/version.hpp>

namespace core
{
    namespace
    {
        constexpr const char* k_settings_file_name = "settings.json";

        // SDL hands paths back as UTF-8; going through char8_t keeps that
        // meaning on Windows, where a narrow std::filesystem::path would be
        // read in the ANSI code page.
        std::filesystem::path utf8_path(const std::string& path)
        {
            return std::filesystem::path{std::u8string(path.begin(), path.end())};
        }

        // <SDL pref path>/settings.json, or empty when the platform has no
        // per-user directory for the application (SDL creates it on demand).
        std::string default_settings_path()
        {
            char* pref_path = SDL_GetPrefPath("AlphaEngine", "AlphaEngine");
            if (pref_path == nullptr)
            {
                LOG_WRN("settings: SDL_GetPrefPath failed (%s); no settings file will be read", SDL_GetError());
                return {};
            }
            std::string path = std::string{pref_path} + k_settings_file_name;
            SDL_free(pref_path);
            return path;
        }

        // Reads the file at `path` and applies it as a settings.json document.
        // A missing file is the normal first run and is only noted; the
        // warnings for a malformed one come from apply_json, right after the
        // INFO line that names the file.
        void apply_settings_file(settings& out, const std::string& path)
        {
            std::ifstream file{utf8_path(path), std::ios::binary};
            if (!file.is_open())
            {
                LOG_INF("Settings: no settings file at %s; continuing with the defaults", path.c_str());
                return;
            }
            const std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
            LOG_INF("Settings: reading %s", path.c_str());
            apply_json(out, text);
        }

        const char* on_off(bool value) noexcept
        {
            return value ? "on" : "off";
        }
    } // namespace

    bool window_settings::uses_native_resolution() const noexcept
    {
        return width == 0 || height == 0;
    }

    float window_settings::aspect_ratio() const noexcept
    {
        if (height == 0)
        {
            return 1.0f;
        }
        return static_cast<float>(width) / static_cast<float>(height);
    }

    settings::settings()
    {
#ifdef _DEBUG
        // Debug runs windowed and roomy: a larger canvas leaves space for the
        // ImGui debug panels (FPS overlay, settings inspector) without
        // crowding the scene.
        window.width = 1600;
        window.height = 900;
        window.mode = window_mode::windowed;
#else
        // Release goes fullscreen at the display's native size. The size is
        // left at zero here — SDL's video subsystem is not up yet — and
        // resolved by window::init once it is.
        window.width = 0;
        window.height = 0;
        window.mode = window_mode::fullscreen;
#endif
        window.title = std::string{"AlphaEngine v"} + core::version::get_version();
    }

    const char* window_mode_name(window_mode mode) noexcept
    {
        switch (mode)
        {
        case window_mode::windowed:
            return "windowed";
        case window_mode::fullscreen:
            return "fullscreen";
        case window_mode::borderless:
            return "borderless";
        }
        return "unknown";
    }

    const char* graphics_backend_name(graphics_backend backend) noexcept
    {
        switch (backend)
        {
        case graphics_backend::opengl:
            return "opengl";
        case graphics_backend::vulkan:
            return "vulkan";
        }
        return "unknown";
    }

    settings_load_result load_settings(int argc, char* const argv[])
    {
        settings_load_result result;

        std::vector<const char*> args;
        if (argv != nullptr)
        {
            for (int i = 1; i < argc; ++i)
            {
                if (argv[i] != nullptr)
                {
                    args.push_back(argv[i]);
                }
            }
        }

        // The command line is parsed first — --settings names the file the
        // next layer reads and --help short-circuits everything — but applied
        // last, so it stays the top layer.
        const command_line_options options = parse_command_line(args);
        if (options.help_requested)
        {
            result.help_requested = true;
            return result;
        }

        const std::string path = options.settings_path.has_value() ? *options.settings_path : default_settings_path();
        if (!path.empty())
        {
            apply_settings_file(result.values, path);
        }
        apply_environment(result.values, [](const char* name) { return std::getenv(name); });
        apply_command_line(result.values, options);

        if (options.log_level.has_value())
        {
            if (logging::configure_levels(*options.log_level))
            {
                LOG_INF("Log level: %s (--log-level=%s)",
                        logging::verbosity_name(logging::level()),
                        options.log_level->c_str());
            }
            else
            {
                LOG_WRN("Unrecognised --log-level='%s'; expected <level>[,<category>=<level>...] with level in "
                        "trace|debug|info|warn|error|fatal; keeping %s",
                        options.log_level->c_str(),
                        logging::verbosity_name(logging::level()));
            }
        }

        const settings& s = result.values;
        LOG_INF("Settings resolved: window=%ux%u%s mode=%s vsync=%s double_buffered=%s backend=%s temporal_aa=%s "
                "fov=%.1f mouse_sensitivity=%.4f mouse_reversed=%s title='%s'",
                s.window.width,
                s.window.height,
                s.window.uses_native_resolution() ? " (match the display)" : "",
                window_mode_name(s.window.mode),
                on_off(s.window.vsync),
                on_off(s.window.double_buffered),
                graphics_backend_name(s.graphics.backend),
                on_off(s.graphics.temporal_aa),
                static_cast<double>(s.camera.field_of_view),
                static_cast<double>(s.input.mouse_sensitivity),
                on_off(s.input.mouse_reversed),
                s.window.title.c_str());
        return result;
    }
} // namespace core
