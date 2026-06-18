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

#pragma once

#include <core/math/math.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/render_graph/frame_graph.hpp>

namespace rendering_engine
{
    /**
     * @brief Per-pixel screen-space motion vectors for temporal reprojection.
     *
     * Writes, for every pixel, the UV-space displacement of the surface
     * under it since the previous frame: @c velocity = thisFrameUV -
     * lastFrameUV. The @ref taa_pass samples this to reproject its colour
     * history along the motion, so a moving camera keeps a sharp,
     * supersampled image instead of relying on the neighbourhood clamp to
     * hide the misalignment.
     *
     * The vectors are reconstructed from the scene depth buffer and the
     * camera matrices alone — a single fullscreen pass, no second geometry
     * draw. Each pixel's depth plus the current inverse view-projection
     * gives its world position; re-projecting that world position through
     * the previous frame's view-projection gives where it sat on screen
     * last frame, and the difference is the motion vector. One
     * @c reprojection matrix (@c prevViewProj * inverse(curViewProj),
     * both unjittered) folds the whole chain into a single transform
     * uploaded per frame. This captures camera motion for the static world;
     * independently animated objects need their own previous-frame
     * transforms (a multi-target geometry pass) and remain future work.
     *
     * Runs after the scene/skybox passes (so the depth buffer is final) and
     * before @ref taa_pass. The result is an @c rgba16f target with the
     * signed motion in @c xy; it is exposed via @ref velocity_texture so
     * the TAA resolve can sample it. Pipeline state mirrors the other
     * fullscreen-triangle passes (depth off, blend off, no culling). A
     * degenerate backbuffer leaves the pass disabled; a frame with no
     * camera clears the target to zero (no motion).
     */
    struct velocity_pass : pass
    {
        // @p scene_depth is the depth attachment of the HDR scene target;
        // @p width / @p height size the velocity target.
        velocity_pass(gpu::texture scene_depth, uint32_t width, uint32_t height);
        ~velocity_pass() override;

        velocity_pass(const velocity_pass&) = delete;
        velocity_pass& operator=(const velocity_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "velocity";
        }

        void declare_io(render_graph::pass_io_builder& io) const override
        {
            io.read("scene_color");
            io.write("velocity");
        }

        // The motion-vector texture the TAA resolve samples (signed UV
        // displacement in xy). Stable across frames; invalid when the pass
        // is disabled (degenerate backbuffer).
        gpu::texture velocity_texture() const;

    private:
        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_fragment_shader{};

        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_reproj_ubo{};

        // {sceneDepth @0, reprojection @1}.
        gpu::bind_group_layout m_layout{};
        gpu::pipeline m_pipeline{};

        gpu::render_target m_velocity_target{};
        gpu::texture m_velocity_texture{};
        gpu::bind_group m_bind_group{};

        // Previous frame's unjittered view-projection, kept so this frame
        // can build the reprojection matrix. @c m_has_prev is false until
        // the first camera frame has run, so that frame reports zero motion
        // instead of reprojecting against an undefined matrix.
        core::math::mat4 m_prev_view_proj{};
        bool m_has_prev{false};

        // False when the backbuffer dimensions are degenerate (no settings,
        // zero-sized window); record() then no-ops and velocity_texture()
        // returns an invalid handle.
        bool m_enabled{false};
    };
} // namespace rendering_engine
