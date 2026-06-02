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

#include <vector>

#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    struct renderable;

    /**
     * @brief Debug-overlay pass. Loads the previous colour, disables
     *        depth, collects draw items from the debug-renderable
     *        registry, sorts them by pipeline, and dispatches them;
     *        broadcasts @ref core::render_debug as the
     *        documented escape hatch for debug-line / gizmo / frustum
     *        / bounds visualisations whose cadence does not match the
     *        registry walk.
     *
     * Only appended to the engine's pass list in debug builds — the
     * `#if _DEBUG` gate at the construction site in
     * @ref context::init drops it from release entirely, so the
     * stage costs nothing in shipping configurations. Inside a debug
     * build it runs last (after the UI pass) so debug visuals always
     * read on top of the game UI.
     *
     * Like @ref ui_pass the matching draw items use @ref ui_material,
     * which has no per-frame bind group, so this pass owns no
     * per-frame state of its own.
     */
    struct debug_pass : pass
    {
        // @p frame_bind_group is the scene pass's per-frame group (camera
        // at slot 0). The debug helpers draw through the line material,
        // whose pipeline reserves slot 0 for the camera, so binding it
        // here projects the world-space gizmos with the same camera the
        // scene used. The scene pass runs first and refills the backing
        // UBO every frame, so the captured handle always reflects the
        // current camera. An invalid handle simply binds nothing (e.g.
        // ImGui-only debug content).
        debug_pass(std::vector<renderable*>* registry, gpu::bind_group frame_bind_group);
        ~debug_pass() override = default;

        debug_pass(const debug_pass&) = delete;
        debug_pass& operator=(const debug_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

    private:
        // Non-owning back-pointer to the engine context's
        // debug-renderable registry. Same lifetime guarantee as
        // @ref ui_pass::m_registry.
        std::vector<renderable*>* m_registry;

        // Scene pass's per-frame camera bind group, bound at slot 0 for
        // the line-based gizmos. See the constructor note.
        gpu::bind_group m_frame_bind_group;

        // Reused across frames so the underlying allocation persists.
        std::vector<draw_item> m_items;
    };
} // namespace rendering_engine
