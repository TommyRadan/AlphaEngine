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
 * @file settings.hpp
 * @brief Engine-wide configuration, grouped per subsystem domain.
 *
 * The values are resolved once at startup by @ref core::load_settings: compiled defaults, then
 * `<pref path>/settings.json`, then the `ALPHAENGINE_*` environment variables, then the command line, each
 * layer overriding the one before it (see docs/settings.md). The per-layer steps are pure functions in
 * core/settings_parse.hpp so they can be exercised without touching the process environment or SDL.
 */

#pragma once

#include <string>

namespace core
{
    /** @brief Presentation mode for the application window. */
    enum class window_mode
    {
        windowed,   /**< Standard decorated window. */
        fullscreen, /**< Fullscreen at the display's resolution. */
        borderless  /**< Borderless window. */
    };

    /** @brief GPU backend selected by @ref graphics_settings. */
    enum class graphics_backend
    {
        opengl, /**< OpenGL 4.6 core. */
        vulkan, /**< Vulkan. */
    };

    /** @brief Window / presentation configuration. */
    struct window_settings
    {
        /**
         * @brief Window size in logical points. Zero on either axis means "match the primary display": that
         *        query needs SDL's video subsystem, so @ref rendering_engine::window::init resolves it after
         *        bringing video up and writes the concrete size back here for every later reader.
         */
        unsigned int width{0};
        unsigned int height{0};
        std::string title;
        window_mode mode{window_mode::windowed};
        bool double_buffered{true};

        /**
         * @brief Whether the presentation engine should wait for vertical
         *        sync. Off by default in both configurations so the frame
         *        rate is uncapped (the debug FPS overlay is more useful with
         *        vsync off, and release favours latency over tearing).
         */
        bool vsync{false};

        /** @brief Whether @ref width or @ref height is still the "match the display" placeholder. */
        bool uses_native_resolution() const noexcept;

        /** @brief Returns @c width / @c height, or 1 while the size is unresolved (@ref uses_native_resolution). */
        float aspect_ratio() const noexcept;
    };

    /** @brief GPU backend configuration. */
    struct graphics_settings
    {
        /** @brief GPU backend the engine brings up at startup. Read once during @ref runtime::engine construction. */
        graphics_backend backend{graphics_backend::vulkan};

        /**
         * @brief Whether temporal anti-aliasing is enabled.
         *
         * When on, the scene pass jitters the projection matrix with a
         * Halton sub-pixel sequence and the @ref rendering_engine::taa_pass
         * accumulates the jittered frames into a stable, supersampled image
         * (a neighbourhood colour clamp keeps moving content from ghosting).
         * Read once during rendering-engine init.
         */
        bool temporal_aa{true};
    };

    /** @brief Camera configuration. */
    struct camera_settings
    {
        /** @brief Vertical field of view of the perspective camera, in degrees. */
        float field_of_view{70.0f};
    };

    /** @brief Input / mouse configuration. */
    struct input_settings
    {
        /** @brief Mouse-look scale, radians per point of cursor travel. */
        float mouse_sensitivity{0.005f};
        bool mouse_reversed{false};
    };

    /**
     * @brief Engine-wide configuration, owned by @ref runtime::engine.
     *
     * Plain data. The default constructor holds the compiled defaults (debug builds: 1600x900 windowed;
     * release builds: fullscreen at the display's native size); @ref load_settings layers the config file, the
     * environment and the command line on top. The engine takes the resolved struct by value at construction
     * and subsystems read it afterwards. Nothing writes to it once the engine is up, except the one documented
     * write-back of the resolved native size in @ref rendering_engine::window::init.
     */
    struct settings
    {
        settings();

        window_settings window;
        graphics_settings graphics;
        camera_settings camera;
        input_settings input;
    };

    /** @brief The lowercase name of @p mode (`windowed`, `fullscreen`, `borderless`). */
    const char* window_mode_name(window_mode mode) noexcept;

    /** @brief The lowercase name of @p backend (`opengl`, `vulkan`). */
    const char* graphics_backend_name(graphics_backend backend) noexcept;

    /** @brief Outcome of @ref load_settings. */
    struct settings_load_result
    {
        settings values;

        /**
         * @brief @c --help (or @c -h) was on the command line: the caller prints @ref command_line_usage and
         *        exits instead of starting the engine. @ref values is left at the compiled defaults.
         */
        bool help_requested{false};
    };

    /**
     * @brief Resolves the process-wide settings: compiled defaults, then `settings.json` under
     *        @c SDL_GetPrefPath("AlphaEngine", "AlphaEngine") (or the file named by @c --settings), then the
     *        `ALPHAENGINE_*` environment variables, then the command line. A missing or malformed file and any
     *        unrecognised value are logged and skipped, never fatal. Applies @c --log-level to
     *        @ref core::logging on the way and logs the resolved values once at INFO. Requires @c LOG_INIT to
     *        have run.
     * @param argc Argument count as given to @c main.
     * @param argv Arguments as given to @c main; @c argv[0] (the program name) is skipped.
     */
    settings_load_result load_settings(int argc, char* const argv[]);
} // namespace core
