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
 */

#pragma once

#include <string>

/** @brief Presentation mode for the application window. */
enum class win_type
{
    win_type_windowed,   /**< Standard decorated window. */
    win_type_borderless, /**< Borderless window. */
    win_type_fullscreen  /**< Exclusive fullscreen. */
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
    unsigned int width;
    unsigned int height;
    std::string name;
    win_type type;
    bool double_buffered;

    /**
     * @brief Whether the presentation engine should wait for vertical
     *        sync. Disabled by default for both debug and release so the
     *        frame rate is uncapped (the debug FPS overlay is more useful
     *        with vsync off, and release favours latency over tearing).
     */
    bool vsync;

    /** @brief Returns @c width / @c height of the window. */
    float aspect_ratio() const noexcept;
};

/** @brief GPU backend configuration. */
struct graphics_settings
{
    /**
     * @brief GPU backend the engine should bring up at startup.
     *
     * Read once during @ref runtime::engine construction. The default
     * is @ref graphics_backend::opengl; the value can be overridden
     * via the @c ALPHAENGINE_GRAPHICS_BACKEND environment variable
     * (`opengl` or `vulkan`).
     */
    graphics_backend backend;

    /**
     * @brief Whether temporal anti-aliasing is enabled.
     *
     * When on, the scene pass jitters the projection matrix with a
     * Halton sub-pixel sequence and the @ref rendering_engine::taa_pass
     * accumulates the jittered frames into a stable, supersampled image
     * (a neighbourhood colour clamp keeps moving content from ghosting).
     * Read once during rendering-engine init. Enabled by default; set
     * the @c ALPHAENGINE_TAA environment variable to `0` / `off` /
     * `false` to disable it (or `1` / `on` / `true` to force it on).
     */
    bool temporal_aa;
};

/** @brief Camera configuration. */
struct camera_settings
{
    float field_of_view;
};

/** @brief Input / mouse configuration. */
struct input_settings
{
    float mouse_sensitivity;
    bool mouse_reversed;
};

/**
 * @brief Engine-wide configuration, owned by @ref runtime::engine.
 *
 * Values are populated in the constructor — debug builds default to a
 * 1600x900 windowed layout, release builds match the current display
 * mode and go fullscreen. The struct is intended as read-only engine-wide
 * configuration, grouped into per-subsystem domains.
 */
struct settings
{
    settings();

    window_settings window;
    graphics_settings graphics;
    camera_settings camera;
    input_settings input;
};
