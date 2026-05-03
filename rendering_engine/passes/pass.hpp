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

/**
 * @file pass.hpp
 * @brief Render-pass interface walked once per frame by the engine.
 */

#pragma once

#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/handle.hpp>

namespace rendering_engine
{
    struct camera;

    /**
     * @brief Per-frame state shared with every pass.
     *
     * Captured once at the top of @ref context::render so passes
     * cannot disagree about which camera or backbuffer is active
     * mid-frame, and so individual passes do not have to re-query
     * the camera singleton on every entry.
     */
    struct frame_context
    {
        // Backbuffer the passes ultimately resolve to. Off-screen
        // targets are owned by the passes that use them; this is
        // the one shared output.
        gpu::render_target swapchain_target{};

        // Active camera, or nullptr if none is attached. Passes that
        // require a camera (the scene pass today) early-return when
        // this is null.
        camera* active_camera{nullptr};
    };

    /**
     * @brief One step in the per-frame render sequence.
     *
     * Implementations record their draws against the encoder. The
     * engine walks an ordered @c std::vector of passes in
     * @ref context::render, so adding a new pass (post-process,
     * shadow, depth pre-pass, debug overlay) is a registration call
     * at startup, not an edit to the engine's render loop.
     *
     * Names follow the industry-standard "pass" terminology even
     * though @ref gpu::render_pass_encoder shares the word; the two
     * live in different namespaces and the existing engine code
     * already uses @c pass as a local for the encoder.
     */
    struct pass
    {
        virtual ~pass() = default;

        /**
         * @brief Records this pass's draws on @p encoder.
         *
         * Called once per frame in registration order. Implementations
         * open their own @ref gpu::render_pass_encoder via
         * @c encoder.begin_render_pass and close it before returning.
         */
        virtual void record(gpu::command_encoder& encoder, const frame_context& ctx) = 0;
    };
} // namespace rendering_engine
