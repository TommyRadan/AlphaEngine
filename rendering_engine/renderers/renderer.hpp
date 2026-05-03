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

#include <string>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/shader.hpp>

namespace rendering_engine
{
    namespace renderers
    {
        // Common base for the engine's built-in renderers. Holds one
        // pipeline plus the bind-group layouts that pipeline references.
        // The frame driver in @c rendering_engine::context::render
        // calls @ref begin to set the pipeline (and any per-frame bind
        // group) on the active pass encoder, broadcasts the appropriate
        // render event so renderables record draws against the encoder,
        // then calls @ref end.
        struct renderer
        {
            virtual ~renderer();

            // The pipeline this renderer was built around.
            gpu::pipeline pipeline_handle() const;

            // The bind-group layout for per-draw resources (model
            // matrix, textures, options). Renderables build a fresh
            // @c gpu::bind_group against this layout each frame and
            // bind it at slot @ref draw_bind_group_slot.
            gpu::bind_group_layout draw_bind_group_layout() const;

            // Slot index used by the per-draw bind group when the
            // renderer also has a per-frame bind group at slot 0.
            uint32_t draw_bind_group_slot() const;

            // Bind the renderer's pipeline on @p encoder. Derived
            // renderers also populate and bind their per-frame bind
            // group here (e.g. camera matrices).
            virtual void begin(gpu::render_pass_encoder& encoder);

            // Counterpart to @ref begin. Default is a no-op; the GL
            // backend's pipeline state stays bound until the next
            // @c set_pipeline.
            virtual void end(gpu::render_pass_encoder& encoder);

        protected:
            renderer() = default;

            // Construct the pipeline + layouts. Stores handles in the
            // protected members below.
            void construct_pipeline(const std::string& vertex_source,
                                    const std::string& fragment_source,
                                    const gpu::vertex_buffer_layout& vertex_layout,
                                    const gpu::bind_group_layout_descriptor& draw_layout,
                                    const gpu::bind_group_layout_descriptor* frame_layout,
                                    const gpu::depth_state& depth,
                                    const gpu::blend_state& blend,
                                    const gpu::rasterizer_state& rasterizer);

            void destruct_pipeline();

            gpu::shader_module m_vertex_shader{};
            gpu::shader_module m_fragment_shader{};
            gpu::pipeline m_pipeline{};
            gpu::bind_group_layout m_draw_layout{};
            gpu::bind_group_layout m_frame_layout{};
        };
    } // namespace renderers

    using renderers::renderer;
} // namespace rendering_engine
