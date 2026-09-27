// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file graphics_settings.hpp
 * @brief GPU backend / renderer configuration and its `settings.json` / `ALPHAENGINE_*` / command-line surface.
 */

#pragma once

namespace rendering_engine
{
    /** @brief GPU backend selected by @ref graphics_settings. */
    enum class graphics_backend
    {
        vulkan, /**< Vulkan. */
    };

    /** @brief The lowercase name of @p backend (`vulkan`). */
    const char* graphics_backend_name(graphics_backend backend) noexcept;

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

        /**
         * @brief Whether the scene starts with the depth pre-pass enabled.
         *
         * When on, @ref rendering_engine::depth_prepass lays the opaque queue's depth into the scene target
         * front-to-back through each material's vertex stage alone, and the scene pass then loads that depth and
         * shades every pre-passed surface with depth writes off and a less-or-equal test, so each covered pixel
         * is shaded once. Off by default. Seeds @c rendering_engine::renderer at init; toggled at runtime with
         * @c renderer::set_depth_prepass.
         */
        bool depth_prepass{false};

        /** @brief Upper bound of @ref frames_in_flight; the Vulkan backend sizes its per-frame rings by it. */
        static constexpr unsigned int max_frames_in_flight = 2;

        /**
         * @brief Frames the Vulkan backend may have in flight at once, 1 to @ref max_frames_in_flight.
         *
         * At 1 the CPU waits for each frame's GPU work before recording the next; at 2 it records frame N+1
         * while the GPU still draws frame N, and every host-written buffer is double-buffered by the device so
         * the two never touch the same memory. Read once during @ref rendering_engine::gpu::device::init.
         */
        unsigned int frames_in_flight{2};

        /**
         * @brief Draw count above which the scene pass records a frame's draws in parallel, and the fewest
         *        draws one recording chunk holds.
         *
         * On the Vulkan backend a scene pass (or depth pre-pass) whose sorted draw list holds more than this
         * many draws is split into contiguous chunks of at least this many draws — at most one per recording
         * thread, the job pool's workers plus the main thread — and every chunk is recorded into its own
         * secondary command buffer at once; at or below it the pass records serially, so the fork-join
         * overhead is only paid where it is amortised. 0 disables the parallel path. Read once during
         * rendering-engine init.
         */
        unsigned int parallel_draw_threshold{512};
    };
} // namespace rendering_engine
