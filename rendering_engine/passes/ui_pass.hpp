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

namespace rendering_engine
{
    struct renderable;

    /**
     * @brief 2D overlay pass. Loads the previous colour, disables
     *        depth, drives @c overlay_renderer over the UI-renderable
     *        registry, and broadcasts @ref event_engine::render_ui
     *        as the documented escape hatch for ImGui-style overlays.
     *
     * Always runs; there is no camera gate. When the scene pass was
     * skipped, the UI is composited over the swapchain's initial
     * (black) clear.
     */
    struct ui_pass : pass
    {
        explicit ui_pass(std::vector<renderable*>* registry);

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

    private:
        // Non-owning back-pointer to the engine context's
        // ui-renderable registry. Same lifetime guarantee as
        // @ref scene_pass::m_registry.
        std::vector<renderable*>* m_registry;
    };
} // namespace rendering_engine
