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
     * @brief 3D scene pass. Clears the swapchain colour and depth,
     *        drives @c basic_renderer over the scene-renderable
     *        registry, and broadcasts @ref event_engine::render_scene
     *        as the documented escape hatch for debug / gizmo
     *        callers.
     *
     * Skipped when no camera is attached (matches the previous
     * @c if (camera != nullptr) gate).
     */
    struct scene_pass : pass
    {
        explicit scene_pass(std::vector<renderable*>* registry);

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

    private:
        // Non-owning back-pointer to the engine context's
        // scene-renderable registry. The context outlives every
        // pass so the pointer stays valid for the pass's lifetime.
        std::vector<renderable*>* m_registry;
    };
} // namespace rendering_engine
