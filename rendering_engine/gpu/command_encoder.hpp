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

/**
 * @file command_encoder.hpp
 * @brief Command-recording interfaces.
 *
 * The shape mirrors the WebGPU / Vulkan recording model: a
 * @ref command_encoder begins zero or more @ref render_pass_encoder
 * scopes and is then submitted through the device. The OpenGL backend
 * implements both interfaces with immediate-mode GL calls — there is no
 * actual command buffer — but the explicit begin/end scope, typed
 * resource binding and pipeline binding match what a Vulkan/D3D12
 * backend would need verbatim.
 */

#pragma once

#include <cstdint>
#include <memory>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct render_pass_encoder
        {
            virtual ~render_pass_encoder() = default;

            // Bind the pipeline state object that subsequent draws will
            // execute against. Must be called before any draw.
            virtual void set_pipeline(pipeline pipeline_handle) = 0;

            // Bind a vertex buffer to the layout slot @p slot. @p slot
            // must match an entry in the active pipeline's
            // @c vertex_buffers vector. @p stride_override, if non-zero,
            // replaces the pipeline-baked stride for this binding so a
            // single shared pipeline can host renderables with
            // different vertex record sizes that share a common
            // attribute prefix.
            virtual void
            set_vertex_buffer(uint32_t slot, buffer buffer_handle, size_t offset = 0, uint32_t stride_override = 0) = 0;

            // Bind an index buffer for subsequent @c draw_indexed calls.
            // @p format selects the index width.
            virtual void set_index_buffer(buffer buffer_handle, index_format format) = 0;

            // Bind a pre-constructed @c bind_group to the layout slot
            // @p group. The pipeline must have been created with a
            // matching @c bind_group_layout at the same index.
            virtual void set_bind_group(uint32_t group, bind_group bind_group_handle) = 0;

            // Override the pass-default viewport. Most callers can leave
            // this alone — @c device::begin_render_pass sets the viewport
            // to the target's full extent automatically.
            virtual void set_viewport(int x, int y, int width, int height) = 0;

            // Issue an unindexed draw of @p vertex_count vertices,
            // starting at vertex index @p first_vertex.
            virtual void draw(uint32_t vertex_count, uint32_t first_vertex = 0) = 0;

            // Issue an indexed draw of @p index_count indices, starting
            // at index @p first_index in the bound index buffer.
            virtual void draw_indexed(uint32_t index_count, uint32_t first_index = 0) = 0;

            // Close the pass. After this call no further methods may be
            // invoked on the encoder. The next pass on the same command
            // encoder may target a different render target.
            virtual void end() = 0;
        };

        struct command_encoder
        {
            virtual ~command_encoder() = default;

            // Open a new render pass scope. The returned encoder is
            // single-use: call its methods to record draws, then
            // @ref render_pass_encoder::end before opening another pass.
            // The return type is @c unique_ptr so the OpenGL backend can
            // allocate a small per-pass state object on the heap; on a
            // future Vulkan backend the encoder would be a thin handle
            // wrapping a @c VkCommandBuffer scope.
            virtual std::unique_ptr<render_pass_encoder>
            begin_render_pass(const render_pass_descriptor& descriptor) = 0;
        };
    } // namespace gpu
} // namespace rendering_engine
