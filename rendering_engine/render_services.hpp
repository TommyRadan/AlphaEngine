// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file render_services.hpp
 * @brief The subsystems and settings the renderer is handed by its owner.
 */

#pragma once

namespace core
{
    struct event_bus;
    struct job_pool;
    struct time;
} // namespace core

namespace platform
{
    struct window;
    struct window_settings;
} // namespace platform

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    struct asset_cache;
    struct graphics_settings;
    struct post_process_settings;
    struct shadow_settings;

    /**
     * @brief What @ref renderer::init is handed: the subsystems the renderer
     *        works through and read-only views of the settings it reads.
     *
     * Non-owning. The owner keeps every pointee alive from
     * @ref renderer::init until @ref renderer::quit returns. The device,
     * the window and the event bus are required; every other member may be
     * null, with the effect its comment names.
     */
    struct render_services
    {
        /** @brief The GPU device the passes, materials and targets are built on. */
        gpu::device* device{nullptr};

        /** @brief The window whose drawable the swapchain and the off-screen targets are sized to. */
        const platform::window* window{nullptr};

        /**
         * @brief The window's settings, whose aspect stands in for the
         *        drawable's while the window reports no drawable. Null
         *        falls back to a square aspect.
         */
        const platform::window_settings* window_settings{nullptr};

        /** @brief The bus whose @c core::window_resized events resize the swapchain and the targets. */
        core::event_bus* events{nullptr};

        /** @brief The pool the scene pass records draw chunks on. Null records every frame serially. */
        core::job_pool* jobs{nullptr};

        /** @brief The cache the colour-grading LUT is loaded through. Null leaves grading off. */
        asset_cache* assets{nullptr};

        /**
         * @brief The engine clock the frame context's time and delta are
         *        read from every frame. Null leaves both zero.
         */
        const core::time* time{nullptr};

        /** @brief The graphics settings read at init. Null uses the compiled defaults. */
        const graphics_settings* graphics{nullptr};

        /** @brief The shadow settings the shadow passes are sized from at init. Null uses the compiled defaults. */
        const shadow_settings* shadows{nullptr};

        /** @brief The post-processing settings the post chain starts from at init. Null uses the compiled defaults. */
        const post_process_settings* post{nullptr};
    };
} // namespace rendering_engine
