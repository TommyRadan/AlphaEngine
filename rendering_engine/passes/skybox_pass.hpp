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
#include <rendering_engine/render_graph/frame_graph.hpp>

namespace rendering_engine
{
    /**
     * @brief Draws a cube-map background into the HDR scene target.
     *
     * Runs after the @ref scene_pass and loads (does not clear) that pass's
     * HDR colour and depth. A single fullscreen triangle is emitted at the
     * far plane (clip @c z = w) with depth testing on but depth writes off,
     * so the sky fills only the texels the scene left at the cleared far
     * depth and scene geometry occludes it everywhere else. Each fragment
     * reconstructs its world-space view direction from the inverse of the
     * camera's projection times its translation-free view matrix and
     * samples the cube, so the sky stays fixed to the world while the
     * camera turns. The HDR result flows through the existing bloom and
     * tonemap chain like any other scene colour.
     *
     * The pass is always present in the pass list but inert until a cube
     * map is supplied through @ref set_cubemap (and skipped on no-camera
     * frames), mirroring how the engine keeps the bloom pass resident but
     * dormant when disabled.
     */
    struct skybox_pass : pass
    {
        skybox_pass();
        ~skybox_pass() override;

        skybox_pass(const skybox_pass&) = delete;
        skybox_pass& operator=(const skybox_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "skybox";
        }

        void declare_io(render_graph::pass_io_builder& io) const override
        {
            io.read("scene_color");
            io.write("scene_color");
        }

        // Set (or clear, with an invalid handle) the cube map sampled as
        // the background. Rebuilds the input bind group; an invalid handle
        // leaves the pass dormant so it records nothing.
        void set_cubemap(gpu::texture cubemap);

    private:
        // Rebuild the input bind group against @ref m_cubemap and the
        // sky UBO. Called by @ref set_cubemap.
        void rebuild_bind_group();

        gpu::texture m_cubemap{};

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_fragment_shader{};
        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_sky_ubo{};
        gpu::bind_group_layout m_input_layout{};
        gpu::bind_group m_input_bind_group{};
        gpu::pipeline m_pipeline{};
    };
} // namespace rendering_engine
