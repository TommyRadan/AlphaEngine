// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/frame_resources.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine::debug_draw
{
    /**
     * @brief Debug-overlay pass. Loads the previous colour, disables
     *        depth, takes the frame's overlay mesh draws
     *        (@ref frame_context::overlay_draws), sorts them by pipeline,
     *        and dispatches them; then records the Dear ImGui overlay's
     *        draw data into the same open pass through the frame's
     *        @ref gpu::overlay_renderer (@ref frame_context::overlay).
     *
     * Everything the pass records is either an overlay mesh proxy's
     * @ref draw_item or ImGui draw data built earlier in the frame, so
     * @ref record runs no event listener and does not depend on the
     * main-thread event bus. Debug-line / gizmo / frustum / bounds
     * visualisations reach it as mesh proxies with
     * @ref mesh_description::overlay set, such as the @ref line_helper
     * family's.
     *
     * Only registered in debug builds — the `#if _DEBUG` gate at the
     * registration in @ref renderer::init drops it from release entirely,
     * so the stage costs nothing in shipping configurations. Inside a
     * debug build it is the @ref render_stage::overlay stage, after the
     * UI pass, so debug visuals always read on top of the game UI.
     *
     * The pass owns no GPU resources of its own: the line-based gizmos
     * bind the scene pass's camera group, looked up in the published
     * @ref frame_resources::scene_view (see @ref record).
     */
    struct debug_pass : pass
    {
        debug_pass() = default;
        ~debug_pass() override = default;

        debug_pass(const debug_pass&) = delete;
        debug_pass& operator=(const debug_pass&) = delete;

        // Gathers and sorts this frame's debug draw items and looks up the
        // swapchain target and the scene pass's camera group.
        void prepare(const frame_context& ctx) override;

        // The debug helpers draw through the line material, whose
        // pipeline reserves slot 0 for the camera, so the pass binds the
        // scene pass's unjittered per-frame group there
        // (@ref scene_view_data::overlay_frame_group): the world-space
        // gizmos project with the camera the scene used, without the
        // projection jitter the TAA resolve would otherwise leave on them.
        // The scene pass prepares first and refills the backing UBO every
        // frame. With no scene view published nothing is bound (ImGui-only
        // debug content). The ImGui draw data is recorded inline into this
        // pass, which is why it never takes the parallel path.
        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return builtin_passes::debug;
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read_optional(frame_resources::scene_view);
            io.read(frame_resources::swapchain);
            io.write(frame_resources::swapchain);
        }

    private:
        // Reused across frames so the underlying allocation persists.
        // Gathered and sorted by prepare(), drawn by record().
        std::vector<draw_item> m_items;

        // The swapchain target and the scene pass's unjittered camera
        // group (invalid without a scene view) this frame, looked up by
        // prepare().
        gpu::render_target m_target{};
        gpu::bind_group m_frame_group{};
    };
} // namespace rendering_engine::debug_draw
