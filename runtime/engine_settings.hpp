// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file engine_settings.hpp
 * @brief The composition root's settings bundle: one struct per module, registered with a
 *        @c core::settings_registry in the fixed order that fixes the `--help` layout and the "resolved"
 *        log lines.
 */

#pragma once

#include <string>

#include <core/input.hpp>
#include <platform/window_settings.hpp>
#include <rendering_engine/camera/camera_settings.hpp>
#include <rendering_engine/graphics_settings.hpp>
#include <rendering_engine/passes/shadow_settings.hpp>
#include <rendering_engine/post_process_settings.hpp>

namespace core
{
    class settings_registry;
}

namespace runtime
{
    /** @brief Content location configuration. Owned by @c runtime: it has no other natural home (see
     *         @c platform::content_root for the discovered default this overrides). */
    struct content_settings
    {
        /**
         * @brief Directory the engine mounts as the root of the virtual filesystem (see core/vfs/vfs.hpp), so
         *        relative asset paths resolve under it. Empty (the default) means "discover it": the first
         *        @c content directory beside the executable or in one of its parents
         *        (@ref platform::content_root). Set from @c content.root in settings.json,
         *        @c ALPHAENGINE_CONTENT_ROOT or @c --content-root.
         */
        std::string root;
    };

    /**
     * @brief Bounded / fail-loud run configuration, read once by @ref engine. Owned by @c runtime: it
     *        configures the main loop itself, not any one subsystem.
     *
     * Exists for headless verification (a CI smoke run under the Vulkan validation layer): a frame limit turns
     * an otherwise-endless @c while(!is_quit_requested()) loop into one that exits on its own, and fail-on-error
     * turns a validation message or any other @c LOG_ERR into a non-zero process exit without the caller having
     * to grep the log.
     */
    struct diagnostics_settings
    {
        /**
         * @brief Frames to render before @ref engine requests a normal quit. 0 (the default) means run
         * forever. Only rendered frames count: a tick skipped while the window is minimized does not advance this
         * counter. Set from @c diagnostics.frame_limit in settings.json, @c ALPHAENGINE_FRAMES or @c --frames.
         */
        unsigned int frame_limit{0};

        /**
         * @brief Whether a @c LOG_ERR logged during the run makes the process exit non-zero, same as a @c LOG_FTL
         * always does. Off by default. Set from @c diagnostics.fail_on_error in settings.json,
         * @c ALPHAENGINE_FAIL_ON_ERROR or @c --fail-on-error (a presence-only flag on the command line).
         */
        bool fail_on_error{false};
    };

    /**
     * @brief Game-time configuration, read once by @ref engine into its clock (@c core::time). Owned by
     *        @c runtime: the scheduler it paces is the world layer's.
     */
    struct time_settings
    {
        /**
         * @brief The game time scale the engine starts at: 1 (the default) is real time, below 1 slows the game
         *        down, above 1 speeds it up and 0 starts it paused, clamped to [0, @c core::time::max_time_scale].
         *        Physics, animation, behaviours, scripts and audio run at it; the frame loop, the editor and the UI
         *        do not. Changed at run time through @c core::time::set_time_scale (the editor's Profiler panel,
         *        Lua's @c time.scale). Set from @c time.scale in settings.json, @c ALPHAENGINE_TIME_SCALE or
         *        @c --time-scale.
         */
        float scale{1.0f};
    };

    /**
     * @brief Every module-owned settings struct the engine is constructed around.
     *
     * Plain data. Each member's own default constructor holds its compiled defaults; @ref register_engine_settings
     * followed by @c core::load_settings layers the config file, the environment and the command line on top.
     * @ref engine takes the resolved struct by value at construction and subsystems read it afterwards. Nothing
     * writes to it once the engine is up, except the one documented write-back of the resolved native window
     * size in @c platform::window::init.
     */
    struct engine_settings
    {
        platform::window_settings window;
        rendering_engine::graphics_settings graphics;
        rendering_engine::camera_settings camera;
        rendering_engine::shadow_settings shadows;
        rendering_engine::post_process_settings post;
        core::input_settings input;
        content_settings content;
        diagnostics_settings diagnostics;
        time_settings time;
    };

    /**
     * @brief Registers every module's settings section against @p registry, in the order that fixes the
     *        `--help` layout and the "resolved" log lines: window, graphics / camera / shadows, input,
     *        content, diagnostics, time, and finally post-processing (registered last so its many options print as
     *        their own trailing `--help` block, matching the historical command-line layout).
     */
    void register_engine_settings(core::settings_registry& registry, engine_settings& out);

    /**
     * @brief Assembles the full `--help` text: the registered sections' own lines (see
     *        @c core::settings_registry::help_lines_for), interleaved with the handful of literal fragments
     *        @c core owns directly (the preamble, `--log-level` / `--settings` / `-h`, the epilogue) and the
     *        post-processing block's own header.
     */
    std::string settings_help_text(const core::settings_registry& registry);
} // namespace runtime
