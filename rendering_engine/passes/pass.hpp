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

#include <rendering_engine/fog.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/handle.hpp>

namespace rendering_engine
{
    struct camera;

    namespace render_graph
    {
        class pass_io_builder;
    }

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
        // Backbuffer the engine presents. The last post pass writes
        // here; the UI pass composites on top of it.
        gpu::render_target swapchain_target{};

        // Active camera, or nullptr if none is attached. Passes that
        // require a camera (the scene pass today) early-return when
        // this is null.
        camera* active_camera{nullptr};

        // Off-screen HDR colour target the scene pass renders into.
        // Owned by @ref context; surfaced here so passes share the
        // handle without reaching back through the engine. Post
        // passes sample @ref scene_color_texture as their input.
        gpu::render_target scene_color_target{};
        gpu::texture scene_color_texture{};

        // Off-screen LDR colour target the tonemap pass resolves into.
        // The swapchain is not sampleable as a shader input, so the
        // final post effect (FXAA) reads its tonemapped source from this
        // intermediate rgba8 target and writes the result to
        // @ref swapchain_target. Also owned by @ref context.
        gpu::render_target ldr_color_target{};
        gpu::texture ldr_color_texture{};

        // Scene-wide atmospheric fog, copied from @ref context::set_fog
        // each frame. The scene pass packs it into the per-view PerFrame
        // UBO so the lit materials can blend toward it by camera
        // distance. Defaults to @ref fog_mode::none (no fog).
        fog_settings fog{};
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

        /**
         * @brief Stable identifier used in frame-graph diagnostics.
         *
         * Defaults to a generic name; passes override it so dependency
         * warnings name the offending stage.
         */
        virtual const char* name() const
        {
            return "pass";
        }

        /**
         * @brief Declares the logical resources this pass reads and writes.
         *
         * Called once when the frame graph is built so it can validate that
         * every read is produced before it is consumed. Defaults to declaring
         * nothing — such a pass is executed in place but invisible to the
         * dependency check. Passes name resources with the stable strings the
         * engine imports (e.g. "scene_color", "swapchain").
         */
        virtual void declare_io(render_graph::pass_io_builder& io) const
        {
            (void)io;
        }

        /**
         * @brief Whether this pass must record on the main thread.
         *
         * The frame graph may record passes on worker threads. A pass that
         * broadcasts an event during record() (the @c render_scene /
         * @c render_ui / @c render_debug escape hatches) runs arbitrary
         * game-module listeners synchronously, and those — like the rest of the
         * engine — are main-thread-only, so such a pass returns true and the
         * graph keeps it on the main thread. Defaults to false.
         */
        virtual bool main_thread_only() const
        {
            return false;
        }
    };
} // namespace rendering_engine
