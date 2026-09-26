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

        // Depth attachment of @ref scene_color_target, sampleable as a
        // shader input once the scene and skybox passes are done with
        // it. Re-read from the target every frame (not cached) so a
        // later resize that swaps the attachment reaches every pass;
        // consumers compare the handle against the one their bind group
        // was built with and rebuild on change (see velocity_pass).
        // Passes that sample it declare @c io.read("scene_depth").
        //
        // Encoding: non-linear depth24, @c .r in [0, 1], holding the
        // window-space depth 0.5 * z_ndc + 0.5. The camera's projection
        // comes from core::math::perspective, which is GL-convention
        // (clip z in [-w, w], NDC z in [-1, 1]); OpenGL keeps its default
        // depth range of [0, 1] and every Vulkan pipeline opts into the
        // same [-1, 1] clip range via VK_EXT_depth_clip_control with a
        // [0, 1] viewport depth, so both backends store the identical
        // value. depth_utils.hpp ships the GLSL to invert it:
        // depth_to_ndc(d) = 2d - 1 and linearize_depth(d, near, far) =
        // near * far / (far - d * (far - near)), the positive view-space
        // distance in [near, far]. The scene pass clears it to 1.0, so
        // untouched background texels linearize to the far plane.
        //
        // Synchronisation is the backends' concern: OpenGL samples the
        // depth texture directly, and the Vulkan render-pass cache rests
        // an off-screen depth attachment in SHADER_READ_ONLY_OPTIMAL
        // between render passes (a pass that loads it, the skybox,
        // resumes from and returns to that layout).
        gpu::texture scene_depth_texture{};

        // Off-screen LDR colour target the tonemap pass resolves into.
        // The swapchain is not sampleable as a shader input, so the
        // final post effect (FXAA) reads its tonemapped source from this
        // intermediate rgba8 target and writes the result to
        // @ref swapchain_target. Also owned by @ref context.
        gpu::render_target ldr_color_target{};
        gpu::texture ldr_color_texture{};

        // Per-pixel motion vectors the velocity pass wrote this frame
        // (signed UV displacement in xy), or an invalid handle while
        // temporal AA is off. The pass owns the target; @ref context
        // publishes the handle here every frame so the TAA resolve can
        // sample it without holding a pointer to its producer, and so a
        // resize that recreates the target is picked up through the same
        // handle comparison as @ref scene_depth_texture.
        gpu::texture velocity_texture{};

        // The TAA resolve of this frame, or an invalid handle while
        // temporal AA is off. The final anti-aliasing pass (FXAA) samples
        // this when valid and @ref ldr_color_texture otherwise, so the
        // swapchain always receives a single anti-aliased image. Published
        // by @ref context from the pass that owns the target.
        gpu::texture taa_resolve_texture{};

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
         * @brief Notifies the pass that the drawable changed size.
         *
         * Called by @ref context::on_resize with the new pixel size,
         * outside any frame (no command encoder is recording), after the
         * context has recreated its scene-colour and LDR targets at that
         * size and before the next @ref record. Never called with a zero
         * dimension. Passes that own full-resolution targets recreate
         * them here (creating the new target before destroying the old
         * one so consumers see a different handle); passes that bake a
         * size-dependent UBO note the new size and rewrite the buffer at
         * their next @ref record, inside the frame bracket, since the
         * previous frame may still be reading it on a deferred-execution
         * backend until @c begin_frame waits. Passes that sample a texture
         * owned by the context or by another pass do not re-plumb here:
         * they compare the handle in @ref frame_context against the one
         * their bind group was built with on every @ref record and
         * rebuild on change, so any recreation reaches them on the next
         * frame. Defaults to a no-op for passes whose resources do not
         * follow the drawable (shadow maps, UI, debug).
         */
        virtual void resize(uint32_t width, uint32_t height)
        {
            (void)width;
            (void)height;
        }
    };
} // namespace rendering_engine
