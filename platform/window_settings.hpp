// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file window_settings.hpp
 * @brief Window / presentation configuration and its `settings.json` / `ALPHAENGINE_*` / command-line surface.
 */

#pragma once

#include <string>

namespace core
{
    class settings_registry;
}

namespace platform
{
    /** @brief Presentation mode for the application window. */
    enum class window_mode
    {
        windowed,   /**< Standard decorated window. */
        fullscreen, /**< Fullscreen at the display's resolution. */
        borderless  /**< Borderless window. */
    };

    /** @brief The lowercase name of @p mode (`windowed`, `fullscreen`, `borderless`). */
    const char* window_mode_name(window_mode mode) noexcept;

    /** @brief Window / presentation configuration. */
    struct window_settings
    {
        window_settings();

        /**
         * @brief Window size in logical points. Zero on either axis means "match the primary display": that
         *        query needs the window system, so @ref window::init resolves it after bringing video up and
         *        writes the concrete size back here for every later reader.
         */
        unsigned int width{0};
        unsigned int height{0};
        std::string title;
        window_mode mode{window_mode::windowed};

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

    /**
     * @brief Registers @ref window_settings's fields (`window.width`, `.height`, `.mode`, `.vsync`, `.title`)
     *        against @p out. Called once, before @c core::load_settings resolves the registry.
     */
    void register_settings(core::settings_registry& registry, window_settings& out);
} // namespace platform
