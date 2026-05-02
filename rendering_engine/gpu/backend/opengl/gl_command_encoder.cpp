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

#include <infrastructure/log.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_translate.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    namespace
    {
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
        m_pipeline_handle = pipeline_handle;
        m_program_id = pipe->program_id;
        m_vao_id = pipe->vao_id;
        m_topology = to_gl_topology(pipe->topology);

        glUseProgram(m_program_id);
        glBindVertexArray(m_vao_id);
        apply_pipeline_state(*pipe);
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
        if (group >= pipe->cached_locations.size())
        {
            LOG_WRN("set_bind_group: group index out of range");
            return;
        }
        auto* layout = m_device.lookup_bind_group_layout(bg->layout);
        if (layout == nullptr)
        {
            LOG_WRN("set_bind_group: bind group layout missing");
            return;
        }

        const auto& slots = pipe->cached_locations[group];

        for (const auto& value : bg->entries)
        {
            // Find this entry's slot index in the layout by
            // matching binding number — the layout describes
            // slots in declaration order, the bind group's
            // entries can be in any order.
            size_t slot_index = layout->descriptor.entries.size();
            for (size_t i = 0; i < layout->descriptor.entries.size(); ++i)
            {
                if (layout->descriptor.entries[i].binding == value.binding &&
                    layout->descriptor.entries[i].kind == value.kind)
                {
                    slot_index = i;
                    break;
                }
            }
            if (slot_index >= slots.size())
            {
                continue;
            }
            const GLint cached = slots[slot_index];

            switch (value.kind)
            {
            case binding_kind::float_value:
                if (cached >= 0)
                {
                    glUniform1f(cached, value.float_value);
                }
                break;
            case binding_kind::int_value:
                if (cached >= 0)
                {
                    glUniform1i(cached, value.int_value);
                }
                break;
            case binding_kind::vec2_value:
                if (cached >= 0)
                {
                    glUniform2f(cached, value.vec2_value.x, value.vec2_value.y);
                }
                break;
            case binding_kind::vec3_value:
                if (cached >= 0)
                {
                    glUniform3f(cached, value.vec3_value.x, value.vec3_value.y, value.vec3_value.z);
                }
                break;
            case binding_kind::vec4_value:
                if (cached >= 0)
                {
                    glUniform4f(cached, value.vec4_value.x, value.vec4_value.y, value.vec4_value.z, value.vec4_value.w);
                }
                break;
            case binding_kind::mat3_value:
                if (cached >= 0)
                {
                    glUniformMatrix3fv(cached, 1, GL_FALSE, value.mat3_value.data());
                }
                break;
            case binding_kind::mat4_value:
                if (cached >= 0)
                {
                    glUniformMatrix4fv(cached, 1, GL_FALSE, value.mat4_value.data());
                }
                break;
            case binding_kind::texture:
            {
                // Decode (uniform_loc, unit) packed at cache time.
                const GLint uniform_loc = static_cast<GLint>(cached >> 8);
                const int unit = cached & 0xFF;
                auto* tex = m_device.lookup_texture(value.texture_value);
                if (tex != nullptr && tex->object_id != 0)
                {
                    glActiveTexture(GL_TEXTURE0 + unit);
                    glBindTexture(tex->target, tex->object_id);
                    if (uniform_loc >= 0)
                    {
                        glUniform1i(uniform_loc, unit);
                    }
                }
                break;
            }
            case binding_kind::sampler:
                // No-op on the OpenGL backend: sampler state
                // is set per-texture at create time.
                break;
            }
        }
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

    // -- gl_command_encoder -----------------------------------------

    gl_command_encoder::gl_command_encoder(gl_device& device) : m_device{device} {}

    std::unique_ptr<render_pass_encoder> gl_command_encoder::begin_render_pass(const render_pass_descriptor& descriptor)
    {
        return std::make_unique<gl_render_pass_encoder>(m_device, descriptor);
    }
} // namespace rendering_engine::gpu::backend::opengl
