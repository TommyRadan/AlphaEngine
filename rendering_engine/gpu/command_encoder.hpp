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
 * actual command buffer — while the Vulkan backend records every call
 * into a @c VkCommandBuffer that @c device::submit queues; the explicit
 * begin/end scope, typed resource binding and pipeline binding are the
 * same for both.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/texture.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu
{
    struct render_pass_encoder
    {
        // Out-of-line virtual destructor: pins the vtable and
        // typeinfo to @c command_encoder.cpp.
        virtual ~render_pass_encoder();

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
        // @p dynamic_offsets carries one byte offset per slot of the
        // group's layout that sets @c has_dynamic_offset, in ascending
        // binding order (Vulkan's @c pDynamicOffsets rule), and must be
        // empty for a layout without one; each is added to that slot's
        // bound range, must be a multiple of
        // @c device_limits::uniform_buffer_offset_alignment and must keep
        // the range inside the buffer. A group whose count does not match
        // is reported and not bound. The same group may be bound again
        // with other offsets — that is the point: one group over one
        // buffer serves every draw.
        virtual void set_bind_group(uint32_t group,
                                    bind_group bind_group_handle,
                                    std::span<const uint32_t> dynamic_offsets = {}) = 0;

        // Write @p size bytes from @p data into the bound pipeline's push
        // constants at byte @p offset, for the draws recorded after it
        // (@c vkCmdPushConstants; the bytes are copied at the call).
        // The bytes must lie in one of the pipeline's
        // @c pipeline_descriptor::push_constant_ranges, and @p stages
        // must be exactly that range's stages; @p offset and @p size are
        // multiples of 4. A push that does not fit the bound pipeline is
        // reported once per pass and dropped. Pushed values survive a
        // switch to a pipeline that declares the same ranges. Needs
        // @c device_features::push_constants: OpenGL has none for SPIR-V
        // programs, so there the call records nothing (reported once per
        // pipeline) and the caller keeps such data in a uniform buffer.
        virtual void push_constants(shader_stages stages, uint32_t offset, uint32_t size, const void* data) = 0;

        // Override the pass-default viewport. Most callers can leave
        // this alone — @c command_encoder::begin_render_pass sets the
        // viewport to the target's full extent automatically. @p x /
        // @p y are the rectangle's bottom-left corner in window
        // (OpenGL) convention on both backends. Also resets the scissor
        // rectangle to the same extent.
        virtual void set_viewport(int x, int y, int width, int height) = 0;

        // Restrict rasterisation to the given rectangle (bottom-left
        // origin, like @ref set_viewport). The pass begins with the
        // scissor covering the whole target; a rectangle of zero
        // width or height discards every fragment.
        virtual void set_scissor(int x, int y, int width, int height) = 0;

        // The reference value the active pipeline's stencil test
        // compares against (both faces). Dynamic state: it may change
        // between draws without a new pipeline. Ignored by pipelines
        // whose @c stencil_state is disabled.
        virtual void set_stencil_reference(uint32_t reference) = 0;

        // Issue an unindexed draw of @p vertex_count vertices,
        // starting at vertex index @p first_vertex, @p instance_count
        // times. Per-instance vertex streams start at record
        // @p first_instance (@c glDrawArraysInstancedBaseInstance;
        // @c vkCmdDraw).
        virtual void draw(uint32_t vertex_count,
                          uint32_t instance_count = 1,
                          uint32_t first_vertex = 0,
                          uint32_t first_instance = 0) = 0;

        // Issue an indexed draw of @p index_count indices, starting
        // at index @p first_index in the bound index buffer,
        // @p instance_count times. @p base_vertex is added to every
        // index before the vertex fetch, so several meshes can share
        // one vertex buffer; per-instance streams start at record
        // @p first_instance
        // (@c glDrawElementsInstancedBaseVertexBaseInstance;
        // @c vkCmdDrawIndexed).
        virtual void draw_indexed(uint32_t index_count,
                                  uint32_t instance_count = 1,
                                  uint32_t first_index = 0,
                                  int32_t base_vertex = 0,
                                  uint32_t first_instance = 0) = 0;

        // Issue an indexed draw whose parameters are sourced from
        // @p indirect_buffer at @p offset. The buffer record at that
        // offset is a five-uint32 @c indexCount, @c instanceCount,
        // @c firstIndex, @c vertexOffset, @c firstInstance — the
        // shape shared by GL's @c DrawElementsIndirectCommand and
        // Vulkan's @c VkDrawIndexedIndirectCommand. The buffer must
        // have been created with @c buffer_usage_indirect.
        virtual void draw_indexed_indirect(buffer indirect_buffer, size_t offset = 0) = 0;

        // Issue @p draw_count back-to-back indexed indirect draws,
        // each with the same five-uint32 record described in
        // @ref draw_indexed_indirect. @p stride is the byte stride
        // between consecutive records (typically 20 for tightly
        // packed records). Maps to @c glMultiDrawElementsIndirect
        // on the OpenGL backend and to @c vkCmdDrawIndexedIndirect
        // on Vulkan.
        virtual void
        multi_draw_indexed_indirect(buffer indirect_buffer, size_t offset, uint32_t draw_count, uint32_t stride) = 0;

        // Escape hatch returning the backend-native command buffer
        // (a @c VkCommandBuffer on Vulkan) as an opaque pointer so a
        // debug overlay can record draws straight into the open render
        // pass — Dear ImGui's @c ImGui_ImplVulkan_RenderDrawData wants
        // the raw command buffer. Returns @c nullptr on backends that
        // record immediately (OpenGL) and therefore have no command
        // buffer to hand out — those overlays issue their draws against
        // the bound framebuffer instead — and on any backend while the
        // pass is not open (it failed to begin, e.g. no swapchain image
        // this frame), so nothing is recorded outside a render pass.
        virtual void* native_command_buffer() const noexcept
        {
            return nullptr;
        }

        // Escape hatch returning the backend-native render pass (a
        // @c VkRenderPass on Vulkan) this encoder is currently recording
        // into, as an opaque pointer, alongside
        // @ref native_command_buffer. Lets a debug overlay that records
        // into this same open pass (Dear ImGui's Vulkan backend) rebuild
        // its own pipeline exactly when the pass it draws into changes,
        // by comparing against the pass it last built for, rather than
        // re-deriving the pass's load/store arguments itself and hoping
        // they still match what the owning pass begins. Returns
        // @c nullptr on backends with no render-pass object (OpenGL) and
        // on any backend while the pass is not open.
        virtual void* native_render_pass() const noexcept
        {
            return nullptr;
        }

        // Close the pass. After this call no further methods may be
        // invoked on the encoder. The next pass on the same command
        // encoder may target a different render target.
        virtual void end() = 0;
    };

    struct compute_pass_encoder
    {
        // Out-of-line virtual destructor: pins the vtable and
        // typeinfo to @c command_encoder.cpp.
        virtual ~compute_pass_encoder();

        // Bind the compute pipeline subsequent dispatches will
        // execute. The pipeline must have been created via
        // @c device::create_compute_pipeline.
        virtual void set_pipeline(pipeline pipeline_handle) = 0;

        // Bind a pre-constructed @c bind_group to the layout slot
        // @p group. The pipeline must have been created with a
        // matching @c bind_group_layout at the same index.
        // @p dynamic_offsets as for @c render_pass_encoder::set_bind_group.
        virtual void set_bind_group(uint32_t group,
                                    bind_group bind_group_handle,
                                    std::span<const uint32_t> dynamic_offsets = {}) = 0;

        // Dispatch @p group_count_x * @p group_count_y *
        // @p group_count_z workgroups. Workgroup size comes from
        // the @c local_size_x / @c local_size_y / @c local_size_z
        // layout qualifiers baked into the compute shader.
        virtual void dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) = 0;

        // Close the pass. After this call no further methods may be
        // invoked on the encoder.
        virtual void end() = 0;
    };

    struct command_encoder
    {
        // Out-of-line virtual destructor: pins the vtable and
        // typeinfo to @c command_encoder.cpp.
        virtual ~command_encoder();

        // Open a new render pass scope. The returned encoder is
        // single-use: call its methods to record draws, then
        // @ref render_pass_encoder::end before opening another pass.
        // The return type is @c unique_ptr so each backend can keep a
        // small per-pass state object: the OpenGL encoder shadows the
        // bound framebuffer and vertex state, the Vulkan one wraps the
        // @c vkCmdBeginRenderPass / @c vkCmdEndRenderPass scope on the
        // frame's command buffer.
        virtual std::unique_ptr<render_pass_encoder> begin_render_pass(const render_pass_descriptor& descriptor) = 0;

        // Open a new compute pass scope. Compute passes never carry
        // attachments; bind a pipeline and bind groups, dispatch,
        // and end. May be opened concurrently with render passes
        // only on backends that allow it (Vulkan does not — keep
        // them disjoint).
        virtual std::unique_ptr<compute_pass_encoder> begin_compute_pass() = 0;

        // Copy @p size bytes from @p src starting at @p src_offset
        // to @p dst starting at @p dst_offset. The source buffer
        // must have been created with @c buffer_usage_copy_src and
        // the destination with @c buffer_usage_copy_dst. The two
        // ranges must not overlap.
        virtual void
        copy_buffer_to_buffer(buffer src, size_t src_offset, buffer dst, size_t dst_offset, size_t size) = 0;

        // Fill @p size bytes of @p buffer_handle starting at
        // @p offset with the 32-bit pattern @p value (broadcast as
        // little-endian, matching @c glClearBufferSubData and
        // Vulkan's @c vkCmdFillBuffer). The buffer must have been
        // created with @c buffer_usage_copy_dst.
        virtual void clear_buffer(buffer buffer_handle, size_t offset, size_t size, uint32_t value) = 0;

        // Insert a memory barrier ordering prior @p src_stage work
        // (which produced data via @p src_access) against
        // subsequent @p dst_stage work (which consumes data via
        // @p dst_access). On the OpenGL backend the @p src_stage /
        // @p src_access pair is informational and the @p dst_access
        // mask drives @c glMemoryBarrier; on Vulkan all four
        // arguments map verbatim.
        virtual void
        barrier(pipeline_stage src_stage, pipeline_stage dst_stage, access_flag src_access, access_flag dst_access) = 0;

        // Copy @p region of @p dst from the tightly packed texels at
        // @p src_offset in @p src (see @ref texture_copy_region for the
        // layout). The buffer must have been created with
        // @c buffer_usage_copy_src and the texture with
        // @c texture_usage_copy_dst. Must not be recorded while a pass
        // is open. Vulkan records the copy, with the layout
        // transitions around it, into the frame's command buffer so it
        // is ordered against the passes recorded before and after it.
        virtual void
        copy_buffer_to_texture(buffer src, size_t src_offset, texture dst, const texture_copy_region& region) = 0;

        // Copy @p region of @p src into @p dst at @p dst_offset as
        // tightly packed texels. The texture must have been created
        // with @c texture_usage_copy_src (render-target attachments
        // the device allocates have it) and the buffer with
        // @c buffer_usage_copy_dst; a host-visible destination
        // (@c dynamic_data / @c stream_data hint) can then be read
        // back once the frame's work has completed. Must not be
        // recorded while a pass is open.
        virtual void
        copy_texture_to_buffer(texture src, const texture_copy_region& region, buffer dst, size_t dst_offset) = 0;

        // Open / close a named region in the recorded stream for
        // graphics debuggers and the driver's debug output
        // (@c glPushDebugGroup / @c glPopDebugGroup;
        // @c vkCmdBeginDebugUtilsLabelEXT / @c vkCmdEndDebugUtilsLabelEXT
        // when @c VK_EXT_debug_utils was enabled). Groups nest; every
        // push is balanced by a pop before the encoder is submitted.
        // No-ops on a device without @c device_features::debug_labels.
        virtual void push_debug_group(const char* name) = 0;
        virtual void pop_debug_group() = 0;

        // Reset @p count queries of @p set from @p first so they can be
        // written again this frame. Vulkan requires a reset before
        // every write (@c vkCmdResetQueryPool, recorded outside a render
        // pass); OpenGL query objects need none, so this is a no-op
        // there. Record it before the frame's first @ref write_timestamp
        // into the set.
        virtual void reset_queries(query_set set, uint32_t first, uint32_t count) = 0;

        // Write the GPU's timestamp into query @p index of @p set once
        // every command recorded before this point has completed
        // (@c glQueryCounter; @c vkCmdWriteTimestamp at the bottom of the
        // pipe). Read back through @c device::resolve_queries after the
        // frame's work has retired; @c device_limits::timestamp_period_ns
        // converts the ticks. No-op on a device without
        // @c device_features::timestamp_queries.
        virtual void write_timestamp(query_set set, uint32_t index) = 0;
    };
} // namespace rendering_engine::gpu
