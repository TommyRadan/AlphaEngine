// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/math.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/frame_resources.hpp>
#include <rendering_engine/passes/pass.hpp>

namespace rendering_engine
{
    /**
     * @brief Per-pixel screen-space motion vectors for temporal reprojection.
     *
     * Writes, for every pixel, the UV-space displacement of the surface
     * under it since the previous frame: @c velocity = thisFrameUV -
     * lastFrameUV. @ref motion_blur_pass smears the HDR image along it, and
     * @ref taa_pass samples it to reproject its colour
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
     * both unjittered — the previous one is
     * @ref frame_context::prev_view_projection) folds the whole chain into
     * a single transform uploaded per frame. The depth was rasterised with
     * the projection offset by @ref frame_context::jitter, so the shader
     * first subtracts that jitter from the pixel's NDC position: the
     * reconstructed point is then the surface actually under the pixel and
     * both ends of the vector sit on the unjittered pixel grid the history
     * accumulates on, so a static camera yields exactly zero motion and a
     * moving one no Halton-indexed sub-pixel bias. This captures camera
     * motion for the static world; independently animated objects need
     * their own previous-frame transforms (a multi-target geometry pass)
     * and remain future work.
     *
     * Runs after the scene/skybox passes (so the depth buffer is final) and
     * before its consumers, @ref motion_blur_pass and @ref taa_pass. The
     * result is an @c rgba16f target with the signed motion in @c xy; it
     * is published as @ref frame_resources::velocity so they can sample
     * it. The pass
     * is always in the chain, but @ref record draws only while one of them
     * runs (temporal AA is on or @ref motion_blur_active holds), so with
     * both off it costs nothing. Pipeline state mirrors the other
     * fullscreen-triangle passes (depth off, blend off, no culling). A
     * frame with no camera, or without a usable previous view-projection
     * (the view's first frame), reports zero motion.
     *
     * Each view gets a target, a reprojection block and an input bind
     * group of its own (see @ref view_data), the target at the view's size.
     */
    struct velocity_pass : pass
    {
        // The scene depth the pass samples is not a constructor input: it
        // is looked up every frame (@ref frame_resources::scene_depth), and
        // a view's input bind group is (re)built whenever that handle
        // differs from the one it was last built against, so a resized
        // scene target is picked up without any re-plumbing.
        explicit velocity_pass(gpu::device& device);
        ~velocity_pass() override;

        velocity_pass(const velocity_pass&) = delete;
        velocity_pass& operator=(const velocity_pass&) = delete;

        // Publishes the motion-vector texture, decides what the frame does
        // (nothing, a clear to zero motion, or the reprojection draw),
        // uploads the reprojection block and rebinds the scene depth when
        // its handle changed.
        void prepare(const frame_context& ctx) override;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return builtin_passes::velocity;
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read(frame_resources::scene_depth);
            io.write(frame_resources::velocity);
        }

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

        // What the pass keeps per view: the rgba16f velocity target at the
        // view's size, the reprojection block, and the input bind group
        // over the view's scene depth (@ref bound_depth, invalid until the
        // view's first camera frame builds the group) and that block.
        struct view_data final : pass_view_state
        {
            explicit view_data(gpu::device& device);
            ~view_data() override;

            view_data(const view_data&) = delete;
            view_data& operator=(const view_data&) = delete;

            // (Re)allocates the target at @p width x @p height: the new
            // one first, so the handle published as
            // frame_resources::velocity changes and the TAA resolve and
            // motion blur rebind; then the old one is released.
            void resize(uint32_t width, uint32_t height);

            gpu::device* device{nullptr};
            gpu::buffer reproj_ubo{};
            gpu::render_target velocity_target{};
            gpu::texture velocity_texture{};
            gpu::bind_group bind_group{};
            gpu::texture bound_depth{};
            uint32_t width{0};
            uint32_t height{0};
        };

        // Rebuilds @p view's input bind group against @p scene_depth and
        // its reprojection UBO, remembering the handle.
        void rebuild_bind_group(view_data& view, gpu::texture scene_depth);

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_fragment_shader{};

        gpu::buffer m_vertex_buffer{};

        // {sceneDepth @0, reprojection @1}.
        gpu::bind_group_layout m_layout{};
        gpu::pipeline m_pipeline{};

        // The view's target and input group this frame's record() draws
        // with, looked up by prepare().
        gpu::render_target m_target{};
        gpu::bind_group m_bind_group{};

        // What this frame's record() does, decided by prepare(): nothing
        // (no consumer this frame), clear the target to zero
        // motion (no camera or no scene depth), or draw the reprojection.
        enum class frame_action
        {
            none,
            clear,
            draw,
        };
        frame_action m_action{frame_action::none};
    };
} // namespace rendering_engine
