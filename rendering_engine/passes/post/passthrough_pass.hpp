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

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>

namespace rendering_engine
{
    /**
     * @brief Trivial post pass that copies the HDR scene-colour
     *        target into the swapchain via a fullscreen triangle.
     *
     * Acts as the proof-of-life for the post-pass scaffold: the
     * scene pass renders into an off-screen rgba16f target, this
     * pass samples it and writes to the swapchain, then the UI
     * pass composites on top. The shape (input bind group + shared
     * fullscreen vertex shader + effect-specific fragment shader +
     * @c draw(3)) is the template the first real effect — tonemap —
     * builds on.
     */
    struct passthrough_pass : pass
    {
        explicit passthrough_pass(gpu::texture input_color);
        ~passthrough_pass() override;

        passthrough_pass(const passthrough_pass&) = delete;
        passthrough_pass& operator=(const passthrough_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

    private:
        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_fragment_shader{};
        gpu::bind_group_layout m_input_layout{};
        gpu::bind_group m_input_bind_group{};
        gpu::pipeline m_pipeline{};
    };
} // namespace rendering_engine
