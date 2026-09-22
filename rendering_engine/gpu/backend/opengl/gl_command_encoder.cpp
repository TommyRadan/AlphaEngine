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

#include <rendering_engine/gpu/backend/opengl/gl_command_encoder.hpp>

#include <array>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_state_cache.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_translate.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    namespace
    {
        const bind_group_layout_entry* find_layout_entry(const bind_group_layout_descriptor& descriptor,
                                                         uint32_t binding)
        {
            for (const auto& entry : descriptor.entries)
            {
                if (entry.binding == binding)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        // Tell the driver the named planes of @p target hold nothing
        // worth keeping: at pass begin for load_op::dont_care (no
        // load, no clear) and at pass end for store_op::dont_care (the
        // results are never read back). A tile-based GPU skips the
        // load / store traffic; a desktop driver treats it as a hint.
        void invalidate_attachments(const gl_render_target& target, bool color, bool depth)
        {
            std::array<GLenum, 3> attachments{};
            GLsizei count = 0;
            const bool default_framebuffer = target.framebuffer_id == 0;
            if (color)
            {
                attachments[count++] = default_framebuffer ? GL_COLOR : GL_COLOR_ATTACHMENT0;
            }
            if (depth && target.has_depth)
            {
                attachments[count++] = default_framebuffer ? GL_DEPTH : GL_DEPTH_ATTACHMENT;
                if (target.has_stencil)
                {
                    attachments[count++] = default_framebuffer ? GL_STENCIL : GL_STENCIL_ATTACHMENT;
                }
            }
            if (count > 0)
            {
                glInvalidateNamedFramebufferData(target.framebuffer_id, count, attachments.data());
            }
        }

        // Drop a pipeline's vertex-array binding shadows once the
        // device's cache epoch has moved on (a pass boundary or an
        // object deletion since they were recorded).
        void sync_binding_shadows(const gl_device& device, gl_pipeline& pipe)
        {
            if (pipe.shadow_epoch == device.state_epoch())
            {
                return;
            }
            for (auto& shadow : pipe.vertex_binding_shadows)
            {
                shadow.known = false;
            }
            pipe.element_buffer_known = false;
            pipe.shadow_epoch = device.state_epoch();
        }

        void apply_bind_group(gl_device& device, const gl_bind_group& bg)
        {
            // Bindings come straight from the SPIR-V @c Binding
            // decoration. UBO / SSBO / texture / image binding
            // namespaces are disjoint in OpenGL, so the same numeric
            // binding can appear on entries of different kinds
            // without collision.
            auto& cache = device.state_cache();
            const auto* layout = device.lookup_bind_group_layout(bg.layout);

            // Units a sampler entry of this group claims keep that
            // sampler; every other texture unit this group touches
            // falls back to the state baked onto the texture (sampler
            // object 0), so a sampler left on the unit by an earlier
            // group or pass never leaks into this draw. A standalone
            // sampler therefore belongs in the same bind group as the
            // texture it samples.
            uint64_t sampler_units = 0;
            for (const auto& value : bg.entries)
            {
                if (value.kind == binding_kind::sampler && value.binding < 64)
                {
                    sampler_units |= uint64_t{1} << value.binding;
                }
            }

            for (const auto& value : bg.entries)
            {
                switch (value.kind)
                {
                case binding_kind::uniform_buffer:
                {
                    auto* buf = device.lookup_buffer(value.buffer_value);
                    if (buf != nullptr && buf->object_id != 0)
                    {
                        cache.bind_uniform_buffer(value.binding, buf->object_id);
                    }
                    break;
                }
                case binding_kind::storage_buffer:
                {
                    auto* buf = device.lookup_buffer(value.buffer_value);
                    if (buf != nullptr && buf->object_id != 0)
                    {
                        cache.bind_storage_buffer(value.binding, buf->object_id);
                    }
                    break;
                }
                case binding_kind::texture:
                {
                    auto* tex = device.lookup_texture(value.texture_value);
                    if (tex != nullptr && tex->object_id != 0)
                    {
                        cache.bind_texture_unit(value.binding, tex->object_id);
                        const bool has_sampler =
                            value.binding < 64 && (sampler_units & (uint64_t{1} << value.binding)) != 0;
                        if (!has_sampler)
                        {
                            cache.bind_sampler(value.binding, 0);
                        }
                    }
                    break;
                }
                case binding_kind::storage_texture:
                {
                    auto* tex = device.lookup_texture(value.texture_value);
                    if (tex == nullptr || tex->object_id == 0 || layout == nullptr)
                    {
                        break;
                    }
                    const auto* entry = find_layout_entry(layout->descriptor, value.binding);
                    if (entry == nullptr)
                    {
                        LOG_WRN("set_bind_group: storage_texture has no matching layout entry");
                        break;
                    }
                    const auto fmt = to_gl_texture_format(entry->storage_format);
                    const GLboolean layered =
                        (tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_CUBE_MAP) ? GL_TRUE : GL_FALSE;
                    // Bind the requested mip level; layered binds every
                    // cube face / volume slice so a compute shader writes
                    // the whole level through an imageCube / image3D.
                    glBindImageTexture(value.binding,
                                       tex->object_id,
                                       static_cast<GLint>(value.storage_level),
                                       layered,
                                       0,
                                       to_gl_storage_access(entry->storage_access_mode),
                                       fmt.internal_format);
                    break;
                }
                case binding_kind::sampler:
                {
                    auto* samp = device.lookup_sampler(value.sampler_value);
                    if (samp != nullptr && samp->object_id != 0)
                    {
                        // The sampler object overrides the texture-baked
                        // state on this unit for the draws that follow.
                        cache.bind_sampler(value.binding, samp->object_id);
                    }
                    break;
                }
                }
            }
        }

        void apply_pipeline_state(gl_state_cache& cache, const gl_pipeline& pipe, bool use_depth)
        {
            cache.set_blend(pipe.blend.enabled,
                            to_gl_blend_factor(pipe.blend.src),
                            to_gl_blend_factor(pipe.blend.dst),
                            to_gl_blend_op(pipe.blend.op));

            if (use_depth)
            {
                cache.set_depth_test(pipe.depth.test_enabled);
                cache.set_depth_write(pipe.depth.write_enabled);
                cache.set_depth_func(to_gl_compare(pipe.depth.compare));
            }
            else
            {
                // A pass without depth (UI, post) neither tests nor
                // writes it, whatever the pipeline asked for — the
                // target may carry a depth plane another pass owns.
                cache.set_depth_test(false);
                cache.set_depth_write(false);
            }

            cache.set_cull(pipe.rasterizer.cull != cull_mode::none, to_gl_cull_face(pipe.rasterizer.cull));
            cache.set_front_face(to_gl_front_face(pipe.rasterizer.front));
            cache.set_polygon_mode(to_gl_polygon_mode(pipe.rasterizer.polygon));
        }
    } // namespace

    // -- gl_render_pass_encoder ---------------------------------------

    gl_render_pass_encoder::gl_render_pass_encoder(gl_device& device, const render_pass_descriptor& descriptor)
        : m_device{device}, m_active{true}
    {
        auto* target = device.lookup_render_target(descriptor.target);
        if (target == nullptr)
        {
            LOG_ERR("begin_render_pass: invalid render target handle");
            m_active = false;
            return;
        }

        m_target = descriptor.target;
        m_color_store = descriptor.color.store;
        m_depth_store = descriptor.depth.store;
        m_use_depth = descriptor.use_depth;

        // Nothing the cache remembers from before this pass is trusted:
        // another pass, or the debug overlay recording into the same
        // context, may have changed anything since.
        m_device.invalidate_state_cache();
        auto& cache = m_device.state_cache();

        cache.bind_framebuffer(target->framebuffer_id);
        cache.set_scissor_test(false);
        if (target->width > 0 && target->height > 0)
        {
            cache.set_viewport(0, 0, static_cast<GLsizei>(target->width), static_cast<GLsizei>(target->height));
        }

        const bool depth_active = descriptor.use_depth && target->has_depth;

        // load_op::dont_care: neither loaded nor cleared, so tell the
        // driver the previous contents are dead.
        invalidate_attachments(*target,
                               descriptor.color.load == load_op::dont_care,
                               depth_active && descriptor.depth.load == load_op::dont_care);

        GLbitfield clear_mask = 0;
        if (descriptor.color.load == load_op::clear)
        {
            glClearColor(descriptor.color.clear_color[0],
                         descriptor.color.clear_color[1],
                         descriptor.color.clear_color[2],
                         descriptor.color.clear_color[3]);
            clear_mask |= GL_COLOR_BUFFER_BIT;
        }
        if (depth_active && descriptor.depth.load == load_op::clear)
        {
            glClearDepth(static_cast<GLdouble>(descriptor.depth.clear_depth));
            clear_mask |= GL_DEPTH_BUFFER_BIT;
            if (target->has_stencil)
            {
                // A packed attachment clears both planes together.
                glClearStencil(0);
                clear_mask |= GL_STENCIL_BUFFER_BIT;
            }
        }
        if (clear_mask != 0)
        {
            if ((clear_mask & GL_DEPTH_BUFFER_BIT) != 0)
            {
                // glClear honours the depth mask, so make sure depth
                // writes are open before clearing — the pipeline state
                // may have flipped it off in a previous pass.
                cache.set_depth_write(true);
            }
            glClear(clear_mask);
        }
    }

    gl_render_pass_encoder::~gl_render_pass_encoder()
    {
        if (m_active)
        {
            end();
        }
    }

    void gl_render_pass_encoder::set_pipeline(pipeline pipeline_handle)
    {
        auto* pipe = m_device.lookup_pipeline(pipeline_handle);
        if (pipe == nullptr)
        {
            LOG_WRN("set_pipeline: invalid pipeline handle");
            return;
        }
        if (pipe->is_compute)
        {
            LOG_WRN("set_pipeline: compute pipeline bound on render pass encoder");
            return;
        }
        m_pipeline_handle = pipeline_handle;
        m_program_id = pipe->program_id;
        m_vao_id = pipe->vao_id;
        m_topology = to_gl_primitive(pipe->topology);

        auto& cache = m_device.state_cache();
        cache.use_program(m_program_id);
        cache.bind_vertex_array(m_vao_id);
        apply_pipeline_state(cache, *pipe, m_use_depth);
        if (pipe->topology == primitive_topology::patches && pipe->patch_control_points > 0)
        {
            glPatchParameteri(GL_PATCH_VERTICES, static_cast<GLint>(pipe->patch_control_points));
        }
        // The element buffer is vertex-array state: a draw may only
        // source indices once set_index_buffer has attached one to
        // this pipeline's VAO.
        m_index_buffer_bound = false;
    }

    void gl_render_pass_encoder::set_vertex_buffer(uint32_t slot,
                                                   buffer buffer_handle,
                                                   size_t offset,
                                                   uint32_t stride_override)
    {
        auto* pipe = m_device.lookup_pipeline(m_pipeline_handle);
        auto* buf = m_device.lookup_buffer(buffer_handle);
        if (pipe == nullptr || buf == nullptr || slot >= pipe->vertex_buffers.size())
        {
            LOG_WRN("set_vertex_buffer: invalid pipeline / buffer / slot");
            return;
        }

        // The attribute formats are baked into the VAO; only the buffer
        // behind the slot's binding point changes here. A binding stride
        // of 0 would make every vertex read the same record (unlike the
        // old attribute-pointer path, where 0 meant tightly packed), so
        // a layout that leaves the stride to the draw and a draw that
        // leaves it to the layout fall back to the record the layout's
        // attributes span.
        const auto& layout = pipe->vertex_buffers[slot];
        uint32_t stride = stride_override != 0 ? stride_override : layout.stride;
        if (stride == 0)
        {
            stride = pipe->min_strides[slot];
        }

        sync_binding_shadows(m_device, *pipe);
        auto& shadow = pipe->vertex_binding_shadows[slot];
        const auto gl_offset = static_cast<GLintptr>(offset);
        const auto gl_stride = static_cast<GLsizei>(stride);
        if (shadow.known && shadow.buffer == buf->object_id && shadow.offset == gl_offset && shadow.stride == gl_stride)
        {
            return;
        }
        glVertexArrayVertexBuffer(pipe->vao_id, slot, buf->object_id, gl_offset, gl_stride);
        shadow.buffer = buf->object_id;
        shadow.offset = gl_offset;
        shadow.stride = gl_stride;
        shadow.known = true;
    }

    void gl_render_pass_encoder::set_index_buffer(buffer buffer_handle, index_format format)
    {
        auto* pipe = m_device.lookup_pipeline(m_pipeline_handle);
        auto* buf = m_device.lookup_buffer(buffer_handle);
        if (pipe == nullptr || buf == nullptr)
        {
            LOG_WRN("set_index_buffer: invalid pipeline / buffer handle");
            return;
        }

        // Attached to the pipeline's VAO by name, never through the
        // GL_ELEMENT_ARRAY_BUFFER target.
        sync_binding_shadows(m_device, *pipe);
        if (!pipe->element_buffer_known || pipe->element_buffer_shadow != buf->object_id)
        {
            glVertexArrayElementBuffer(pipe->vao_id, buf->object_id);
            pipe->element_buffer_shadow = buf->object_id;
            pipe->element_buffer_known = true;
        }
        m_index_type = to_gl_index_type(format);
        m_index_size = format == index_format::uint16 ? 2 : 4;
        m_index_buffer_bound = true;
    }

    void gl_render_pass_encoder::set_bind_group(uint32_t group, bind_group bind_group_handle)
    {
        auto* pipe = m_device.lookup_pipeline(m_pipeline_handle);
        auto* bg = m_device.lookup_bind_group(bind_group_handle);
        if (pipe == nullptr || bg == nullptr)
        {
            LOG_WRN("set_bind_group: invalid pipeline / bind_group");
            return;
        }
        if (group >= pipe->bind_group_layouts.size())
        {
            LOG_WRN("set_bind_group: group index out of range");
            return;
        }
        apply_bind_group(m_device, *bg);
    }

    void gl_render_pass_encoder::set_viewport(int x, int y, int width, int height)
    {
        m_device.state_cache().set_viewport(x, y, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    }

    void gl_render_pass_encoder::draw(uint32_t vertex_count, uint32_t first_vertex)
    {
        glDrawArrays(m_topology, static_cast<GLint>(first_vertex), static_cast<GLsizei>(vertex_count));
    }

    void gl_render_pass_encoder::draw_indexed(uint32_t index_count, uint32_t first_index)
    {
        if (!m_index_buffer_bound)
        {
            LOG_WRN("draw_indexed: no index buffer bound");
            return;
        }
        const auto byte_offset = static_cast<intptr_t>(first_index) * m_index_size;
        glDrawElements(
            m_topology, static_cast<GLsizei>(index_count), m_index_type, reinterpret_cast<const GLvoid*>(byte_offset));
    }

    void gl_render_pass_encoder::draw_indexed_indirect(buffer indirect_buffer, size_t offset)
    {
        if (!m_index_buffer_bound)
        {
            LOG_WRN("draw_indexed_indirect: no index buffer bound");
            return;
        }
        auto* indirect = m_device.lookup_buffer(indirect_buffer);
        if (indirect == nullptr || indirect->object_id == 0)
        {
            LOG_WRN("draw_indexed_indirect: invalid indirect buffer");
            return;
        }
        m_device.state_cache().bind_draw_indirect_buffer(indirect->object_id);
        glDrawElementsIndirect(
            m_topology, m_index_type, reinterpret_cast<const GLvoid*>(static_cast<intptr_t>(offset)));
    }

    void gl_render_pass_encoder::multi_draw_indexed_indirect(buffer indirect_buffer,
                                                             size_t offset,
                                                             uint32_t draw_count,
                                                             uint32_t stride)
    {
        if (!m_index_buffer_bound)
        {
            LOG_WRN("multi_draw_indexed_indirect: no index buffer bound");
            return;
        }
        auto* indirect = m_device.lookup_buffer(indirect_buffer);
        if (indirect == nullptr || indirect->object_id == 0)
        {
            LOG_WRN("multi_draw_indexed_indirect: invalid indirect buffer");
            return;
        }
        m_device.state_cache().bind_draw_indirect_buffer(indirect->object_id);
        glMultiDrawElementsIndirect(m_topology,
                                    m_index_type,
                                    reinterpret_cast<const GLvoid*>(static_cast<intptr_t>(offset)),
                                    static_cast<GLsizei>(draw_count),
                                    static_cast<GLsizei>(stride));
    }

    void gl_render_pass_encoder::end()
    {
        if (!m_active)
        {
            return;
        }

        // store_op::dont_care: nothing downstream reads these planes,
        // so the driver may drop them instead of resolving them.
        if (const auto* target = m_device.lookup_render_target(m_target))
        {
            invalidate_attachments(
                *target, m_color_store == store_op::dont_care, m_use_depth && m_depth_store == store_op::dont_care);
        }

        // Leave the context with nothing of this pass bound: the next
        // pass (or the window's present) starts from framebuffer 0, and
        // an outside recorder never inherits a VAO or program.
        auto& cache = m_device.state_cache();
        cache.bind_vertex_array(0);
        cache.use_program(0);
        cache.bind_framebuffer(0);
        m_device.invalidate_state_cache();
        m_active = false;
    }

    // -- gl_compute_pass_encoder ----------------------------------

    gl_compute_pass_encoder::gl_compute_pass_encoder(gl_device& device) : m_device{device}, m_active{true}
    {
        m_device.invalidate_state_cache();
    }

    gl_compute_pass_encoder::~gl_compute_pass_encoder()
    {
        if (m_active)
        {
            end();
        }
    }

    void gl_compute_pass_encoder::set_pipeline(pipeline pipeline_handle)
    {
        auto* pipe = m_device.lookup_pipeline(pipeline_handle);
        if (pipe == nullptr)
        {
            LOG_WRN("compute set_pipeline: invalid pipeline handle");
            return;
        }
        if (!pipe->is_compute)
        {
            LOG_WRN("compute set_pipeline: graphics pipeline bound on compute pass encoder");
            return;
        }
        m_pipeline_handle = pipeline_handle;
        m_program_id = pipe->program_id;
        m_device.state_cache().use_program(m_program_id);
    }

    void gl_compute_pass_encoder::set_bind_group(uint32_t group, bind_group bind_group_handle)
    {
        auto* pipe = m_device.lookup_pipeline(m_pipeline_handle);
        auto* bg = m_device.lookup_bind_group(bind_group_handle);
        if (pipe == nullptr || bg == nullptr)
        {
            LOG_WRN("compute set_bind_group: invalid pipeline / bind_group");
            return;
        }
        if (group >= pipe->bind_group_layouts.size())
        {
            LOG_WRN("compute set_bind_group: group index out of range");
            return;
        }
        apply_bind_group(m_device, *bg);
    }

    void gl_compute_pass_encoder::dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
    {
        if (m_program_id == 0)
        {
            LOG_WRN("dispatch: no compute pipeline bound");
            return;
        }
        glDispatchCompute(group_count_x, group_count_y, group_count_z);
    }

    void gl_compute_pass_encoder::end()
    {
        if (!m_active)
        {
            return;
        }
        m_device.state_cache().use_program(0);
        m_device.invalidate_state_cache();
        m_active = false;
    }

    // -- gl_command_encoder -----------------------------------------

    gl_command_encoder::gl_command_encoder(gl_device& device) : m_device{device} {}

    std::unique_ptr<render_pass_encoder> gl_command_encoder::begin_render_pass(const render_pass_descriptor& descriptor)
    {
        return std::make_unique<gl_render_pass_encoder>(m_device, descriptor);
    }

    std::unique_ptr<compute_pass_encoder> gl_command_encoder::begin_compute_pass()
    {
        return std::make_unique<gl_compute_pass_encoder>(m_device);
    }

    void
    gl_command_encoder::copy_buffer_to_buffer(buffer src, size_t src_offset, buffer dst, size_t dst_offset, size_t size)
    {
        auto* src_record = m_device.lookup_buffer(src);
        auto* dst_record = m_device.lookup_buffer(dst);
        if (src_record == nullptr || src_record->object_id == 0 || dst_record == nullptr || dst_record->object_id == 0)
        {
            LOG_WRN("copy_buffer_to_buffer: invalid src / dst buffer");
            return;
        }
        // Named copy: no binding to the copy targets, so a pass that
        // is open around this call keeps its state.
        glCopyNamedBufferSubData(src_record->object_id,
                                 dst_record->object_id,
                                 static_cast<GLintptr>(src_offset),
                                 static_cast<GLintptr>(dst_offset),
                                 static_cast<GLsizeiptr>(size));
    }

    void gl_command_encoder::clear_buffer(buffer buffer_handle, size_t offset, size_t size, uint32_t value)
    {
        auto* record = m_device.lookup_buffer(buffer_handle);
        if (record == nullptr || record->object_id == 0)
        {
            LOG_WRN("clear_buffer: invalid buffer handle");
            return;
        }
        glClearNamedBufferSubData(record->object_id,
                                  GL_R32UI,
                                  static_cast<GLintptr>(offset),
                                  static_cast<GLsizeiptr>(size),
                                  GL_RED_INTEGER,
                                  GL_UNSIGNED_INT,
                                  &value);
    }

    void gl_command_encoder::barrier(pipeline_stage /*src_stage*/,
                                     pipeline_stage /*dst_stage*/,
                                     access_flag /*src_access*/,
                                     access_flag dst_access)
    {
        // OpenGL collapses pipeline-stage information into the
        // memory-barrier bitmask; the @c src / @c dst stages are
        // present for Vulkan portability and ignored here.
        glMemoryBarrier(to_gl_memory_barrier_bits(dst_access));
    }
} // namespace rendering_engine::gpu::backend::opengl
