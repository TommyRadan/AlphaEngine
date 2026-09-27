// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>

namespace rendering_engine
{
    /**
     * @brief Optional depth pre-pass ahead of the @ref scene_pass.
     *
     * Opt-in (@ref frame_context::depth_prepass, from
     * @c rendering_engine::graphics_settings::depth_prepass and
     * @ref renderer::set_depth_prepass). While it is off, or no camera is
     * active, the pass records nothing and the scene pass clears and
     * writes the scene depth itself as it always has; it stays in the
     * pass list either way so the setting can flip at runtime.
     *
     * When it runs it lays the opaque queue's depth into the scene
     * target's depth attachment, through a depth-only render target that
     * imports that attachment (the depth-only targets the shadow passes
     * use), before any colour is shaded. The draw list is the scene
     * pass's own (@ref scene_pass::prepare), reached through
     * @ref frame_context::scene — the scene pass prepares and records
     * right after this one and owns the draw list, the per-frame bind
     * group and the choice between loading and clearing the depth. This
     * pass's @ref prepare decides whether it runs (the setting, a camera,
     * a usable target) and announces a frame it runs to the scene pass
     * (@ref scene_pass::expect_depth_prepass), whose prepare then
     * resolves the depth-only twins; @ref record hands the scene pass
     * the depth-only pass to begin and dispatch
     * (@ref scene_pass::record_depth_prepass, in parallel above the draw
     * threshold like the shading pass). The list is layer-filtered and
     * frustum-culled against the camera and sorted by
     * @ref draw_item::sort_key, so the opaque items it draws go
     * front-to-back; the transparent queue and every surface
     * @ref material::draws_in_depth_prepass rejects (blended, not
     * depth-tested or not depth-writing, or a template whose fragment
     * stage decides coverage) are skipped. The scene pass then loads the
     * depth and draws each pre-passed surface with depth writes off and a
     * less-or-equal test (@ref material::depth_prepassed_pipeline), so a
     * covered pixel is shaded once, while everything the pre-pass skipped
     * keeps its ordinary less-and-write variant against the same buffer.
     *
     * Both passes must produce bit-identical depth for a surface. The
     * pre-pass therefore draws each item through its material's own
     * pipeline variant minus the fragment stage
     * (@ref material::depth_prepass_pipeline): the same vertex module
     * (skinning included), vertex layout and rasterizer state, the same
     * per-frame group with the TAA-jittered view-projection, the same
     * pushed per-draw block, into a target of the same extent; and the
     * vertex shaders declare @c gl_Position
     * @c invariant so the two pipelines may not compile the position
     * differently.
     *
     * The pass list sees it write @c scene_depth, which the scene pass
     * reads (loads) and writes after it. Later depth consumers get the
     * same depth either way.
     */
    struct depth_prepass : pass
    {
        depth_prepass();
        ~depth_prepass() override;

        depth_prepass(const depth_prepass&) = delete;
        depth_prepass& operator=(const depth_prepass&) = delete;

        // Decides whether the pass runs this frame, rebuilds the
        // depth-only target over the frame's scene depth when that
        // changed, and tells the scene pass to expect it.
        void prepare(const frame_context& ctx) override;

        // Records the depth-only pass through the scene pass on a frame
        // @ref prepare decided to run; nothing otherwise.
        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "depth_prepass";
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.write("scene_depth");
        }

        // Drops the depth-only target: it imports the scene target's
        // depth attachment, which the renderer has just recreated at the
        // new size, so the next @ref prepare rebuilds it over the new one.
        void resize(uint32_t width, uint32_t height) override;

    private:
        void release_target();

        // Depth-only target over @ref m_target_depth, the scene depth
        // attachment it was built against. Rebuilt whenever
        // @ref frame_context::scene_depth_texture publishes another
        // handle; the attachment itself stays owned by the renderer's
        // scene-colour target.
        gpu::render_target m_target{};
        gpu::texture m_target_depth{};

        // Whether this frame's @ref prepare decided the pass runs (and
        // announced it to the scene pass), so @ref record draws.
        bool m_active{false};
    };
} // namespace rendering_engine
