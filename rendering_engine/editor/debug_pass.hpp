// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <vector>

#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    struct renderable;
}

namespace rendering_engine::editor
{
    /**
     * @brief Debug-overlay pass. Loads the previous colour, disables
     *        depth, collects draw items from the debug-renderable
     *        registry, sorts them by pipeline, and dispatches them;
     *        then records the Dear ImGui overlay's draw data into the
     *        same open pass through @ref record_draw_data.
     *
     * Everything the pass records is either a registered renderable's
     * @ref draw_item or ImGui draw data built earlier in the frame, so
     * @ref record runs no event listener and does not depend on the
     * main-thread event bus. Debug-line / gizmo / frustum / bounds
     * visualisations reach it through
     * @ref renderer::register_debug_renderable.
     *
     * Only appended to the engine's pass list in debug builds — the
     * `#if _DEBUG` gate at the construction site in
     * @ref renderer::init drops it from release entirely, so the
     * stage costs nothing in shipping configurations. Inside a debug
     * build it runs last (after the UI pass) so debug visuals always
     * read on top of the game UI.
     *
     * The pass owns no per-frame state of its own: the line-based
     * gizmos read the scene pass's camera group through the frame
     * context (see @ref record).
     */
    struct debug_pass : pass
    {
        explicit debug_pass(const std::vector<renderable*>* registry);
        ~debug_pass() override = default;

        debug_pass(const debug_pass&) = delete;
        debug_pass& operator=(const debug_pass&) = delete;

        // Collects and sorts this frame's debug draw items.
        void prepare(const frame_context& ctx) override;

        // The debug helpers draw through the line material, whose
        // pipeline reserves slot 0 for the camera, so the pass binds the
        // scene pass's unjittered per-frame group there
        // (@ref scene_pass::overlay_frame_bind_group, read through
        // @ref frame_context::scene): the world-space gizmos project with
        // the camera the scene used, without the projection jitter the
        // TAA resolve would otherwise leave on them. The scene pass runs
        // first and refills the backing UBO every frame. With no scene
        // pass nothing is bound (ImGui-only debug content). The ImGui
        // draw data is recorded inline into this pass, which is why it
        // never takes the parallel path.
        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "debug";
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read("swapchain");
            io.write("swapchain");
        }

    private:
        // Non-owning back-pointer to the render world's
        // debug-renderable registry. Same lifetime guarantee as
        // @ref ui_pass::m_registry.
        const std::vector<renderable*>* m_registry;

        // Reused across frames so the underlying allocation persists.
        // Collected and sorted by prepare(), drawn by record().
        std::vector<draw_item> m_items;
    };
} // namespace rendering_engine::editor
