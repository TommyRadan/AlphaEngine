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

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>
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

        void apply_bind_group(gl_device& device, const gl_bind_group& bg)
        {
            // Bindings come straight from the SPIR-V @c Binding
            // decoration. UBO / SSBO / texture / image binding
            // namespaces are disjoint in OpenGL, so the same numeric
            // binding can appear on entries of different kinds
            // without collision.
            const auto* layout = device.lookup_bind_group_layout(bg.layout);
            for (const auto& value : bg.entries)
            {
                switch (value.kind)
                {
                case binding_kind::uniform_buffer:
                {
                    auto* buf = device.lookup_buffer(value.buffer_value);
                    if (buf != nullptr && buf->object_id != 0)
                    {
                        glBindBufferBase(GL_UNIFORM_BUFFER, value.binding, buf->object_id);
                    }
                    break;
                }
                case binding_kind::storage_buffer:
                {
                    auto* buf = device.lookup_buffer(value.buffer_value);
                    if (buf != nullptr && buf->object_id != 0)
                    {
                        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, value.binding, buf->object_id);
                    }
                    break;
                }
                case binding_kind::texture:
                {
                    auto* tex = device.lookup_texture(value.texture_value);
                    if (tex != nullptr && tex->object_id != 0)
                    {
                        glActiveTexture(GL_TEXTURE0 + value.binding);
                        glBindTexture(tex->target, tex->object_id);
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
                                       static_cast<GLenum>(fmt.internal_format));
                    break;
                }
                case binding_kind::sampler:
                {
                    auto* samp = device.lookup_sampler(value.sampler_value);
                    (void)samp;
                    // Sampler state is currently baked into the
                    // texture object at create time — no separate
                    // sampler object is bound. Reserved here so a
                    // future Vulkan-style separate-sampler path
                    // slots in without churning the call sites.
                    break;
                }
                }
            }
        }

        void apply_pipeline_state(const gl_pipeline& pipe)
        {
            // Blend
            if (pipe.blend.enabled)
            {
                glEnable(GL_BLEND);
                glBlendFunc(to_gl_blend_factor(pipe.blend.src), to_gl_blend_factor(pipe.blend.dst));
                glBlendEquation(to_gl_blend_op(pipe.blend.op));
            }
            else
            {
                glDisable(GL_BLEND);
            }

            // Depth
            if (pipe.depth.test_enabled)
            {
                glEnable(GL_DEPTH_TEST);
            }
            else
            {
                glDisable(GL_DEPTH_TEST);
            }
            glDepthMask(pipe.depth.write_enabled ? GL_TRUE : GL_FALSE);
            glDepthFunc(to_gl_compare(pipe.depth.compare));

            // Rasterizer
            if (pipe.rasterizer.cull == cull_mode::none)
            {
                glDisable(GL_CULL_FACE);
            }
            else
            {
                glEnable(GL_CULL_FACE);
                glCullFace(to_gl_cull_face(pipe.rasterizer.cull));
            }
            glFrontFace(to_gl_front_face(pipe.rasterizer.front));
            glPolygonMode(GL_FRONT_AND_BACK, to_gl_polygon_mode(pipe.rasterizer.polygon));
        }

        GLenum to_gl_topology(primitive_topology t)
        {
            return to_gl_primitive(t);
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

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->framebuffer_id);

        if (target->width > 0 && target->height > 0)
        {
            glViewport(0, 0, static_cast<GLsizei>(target->width), static_cast<GLsizei>(target->height));
        }

        GLbitfield clear_mask = 0;
        if (descriptor.color.load == load_op::clear)
        {
            glClearColor(descriptor.color.clear_color[0],
                         descriptor.color.clear_color[1],
                         descriptor.color.clear_color[2],
                         descriptor.color.clear_color[3]);
            clear_mask |= GL_COLOR_BUFFER_BIT;
        }
        if (descriptor.use_depth && target->has_depth && descriptor.depth.load == load_op::clear)
        {
            glClearDepth(static_cast<GLdouble>(descriptor.depth.clear_depth));
            clear_mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (clear_mask != 0)
        {
            // glClear honours the depth mask, so make sure
            // depth writes are open before clearing — the
            // pipeline state may have flipped it off in a
            // previous pass.
            glDepthMask(GL_TRUE);
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
        m_topology = to_gl_topology(pipe->topology);

        glUseProgram(m_program_id);
        glBindVertexArray(m_vao_id);
        apply_pipeline_state(*pipe);
        if (pipe->topology == primitive_topology::patches && pipe->patch_control_points > 0)
        {
            glPatchParameteri(GL_PATCH_VERTICES, static_cast<GLint>(pipe->patch_control_points));
        }
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

        glBindVertexArray(pipe->vao_id);
        glBindBuffer(GL_ARRAY_BUFFER, buf->object_id);

        const auto& layout = pipe->vertex_buffers[slot];
        const uint32_t stride = stride_override != 0 ? stride_override : layout.stride;
        // Per-instance slots advance once per instance (divisor 1) instead
        // of once per vertex, so a single record drives a whole instanced
        // draw copy. The divisor is VAO state, so it is set alongside the
        // attribute pointer here.
        const GLuint divisor = layout.step_mode == vertex_step_mode::instance ? 1u : 0u;
        for (const auto& attribute : layout.attributes)
        {
            const GLsizeiptr final_offset = static_cast<GLsizeiptr>(offset + attribute.offset);
            glEnableVertexAttribArray(attribute.location);
            glVertexAttribPointer(attribute.location,
                                  static_cast<GLint>(attribute.components),
                                  to_gl_scalar(attribute.type),
                                  GL_FALSE,
                                  static_cast<GLsizei>(stride),
                                  reinterpret_cast<const GLvoid*>(final_offset));
            glVertexAttribDivisor(attribute.location, divisor);
        }
    }

    void gl_render_pass_encoder::set_index_buffer(buffer buffer_handle, index_format format)
    {
        auto* buf = m_device.lookup_buffer(buffer_handle);
        if (buf == nullptr)
        {
            LOG_WRN("set_index_buffer: invalid buffer handle");
            return;
        }
        glBindVertexArray(m_vao_id);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf->object_id);
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
        glViewport(x, y, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
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
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect->object_id);
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
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect->object_id);
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
        glBindVertexArray(0);
        glUseProgram(0);
        m_active = false;
    }

    // -- gl_compute_pass_encoder ----------------------------------

    gl_compute_pass_encoder::gl_compute_pass_encoder(gl_device& device) : m_device{device}, m_active{true} {}

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
        glUseProgram(m_program_id);
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
        glUseProgram(0);
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
        glBindBuffer(GL_COPY_READ_BUFFER, src_record->object_id);
        glBindBuffer(GL_COPY_WRITE_BUFFER, dst_record->object_id);
        glCopyBufferSubData(GL_COPY_READ_BUFFER,
                            GL_COPY_WRITE_BUFFER,
                            static_cast<GLintptr>(src_offset),
                            static_cast<GLintptr>(dst_offset),
                            static_cast<GLsizeiptr>(size));
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    }

    void gl_command_encoder::clear_buffer(buffer buffer_handle, size_t offset, size_t size, uint32_t value)
    {
        auto* record = m_device.lookup_buffer(buffer_handle);
        if (record == nullptr || record->object_id == 0)
        {
            LOG_WRN("clear_buffer: invalid buffer handle");
            return;
        }
        glBindBuffer(GL_COPY_WRITE_BUFFER, record->object_id);
        glClearBufferSubData(GL_COPY_WRITE_BUFFER,
                             GL_R32UI,
                             static_cast<GLintptr>(offset),
                             static_cast<GLsizeiptr>(size),
                             GL_RED_INTEGER,
                             GL_UNSIGNED_INT,
                             &value);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
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
