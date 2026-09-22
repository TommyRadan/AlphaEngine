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
 * @file gl_device_pipeline.cpp
 * @brief @c gl_device member functions that build pipeline state
 *        objects, bind-group layouts, and bind groups.
 */

#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

#include <glad/gl.h>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_check.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_translate.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    bind_group_layout gl_device::create_bind_group_layout(const bind_group_layout_descriptor& descriptor)
    {
        gl_bind_group_layout record{};
        record.descriptor = descriptor;
        bind_group_layout h{};
        h.id = m_bind_group_layouts.insert(record);
        return h;
    }

    void gl_device::destroy(bind_group_layout handle)
    {
        m_bind_group_layouts.remove(handle.id);
    }

    namespace
    {
        void check_program_link(GLuint program_id)
        {
            GLint linked = 0;
            glGetProgramiv(program_id, GL_LINK_STATUS, &linked);
            if (linked == GL_TRUE)
            {
                return;
            }
            GLint log_length = 0;
            glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &log_length);
            std::string info_log(log_length > 0 ? static_cast<size_t>(log_length) : 1u, '\0');
            if (log_length > 0)
            {
                glGetProgramInfoLog(program_id, log_length, nullptr, info_log.data());
            }
            LOG_ERR("Program link failed: %s", info_log.c_str());
            glDeleteProgram(program_id);
            throw std::runtime_error{info_log};
        }

        // The narrowest vertex record @p layout reads: the end of its
        // furthest attribute.
        uint32_t layout_min_stride(const vertex_buffer_layout& layout)
        {
            uint32_t extent = 0;
            for (const auto& attribute : layout.attributes)
            {
                extent = std::max(extent, attribute.offset + attribute.components * to_gl_scalar_bytes(attribute.type));
            }
            return extent;
        }

        // Bake the pipeline's vertex format into its vertex array: one
        // binding point per layout slot carrying that slot's step rate,
        // and per attribute its component count / type / normalisation
        // and offset within the record, tied to its slot. The encoder
        // then only attaches buffers to the binding points
        // (glVertexArrayVertexBuffer) and never re-specifies a format.
        void bake_vertex_format(GLuint vao, const std::vector<vertex_buffer_layout>& layouts)
        {
            GLint max_relative_offset = 2047;
            glGetIntegerv(GL_MAX_VERTEX_ATTRIB_RELATIVE_OFFSET, &max_relative_offset);

            for (uint32_t slot = 0; slot < layouts.size(); ++slot)
            {
                const auto& layout = layouts[slot];
                // Per-instance slots advance once per instance (divisor
                // 1) instead of once per vertex, so a single record
                // drives a whole instanced draw copy.
                const GLuint divisor = layout.step_mode == vertex_step_mode::instance ? 1u : 0u;
                glVertexArrayBindingDivisor(vao, slot, divisor);

                for (const auto& attribute : layout.attributes)
                {
                    if (attribute.offset > static_cast<uint32_t>(max_relative_offset))
                    {
                        LOG_WRN("create_pipeline: attribute %u offset %u exceeds the relative-offset limit (%i)",
                                attribute.location,
                                attribute.offset,
                                max_relative_offset);
                    }
                    glEnableVertexArrayAttrib(vao, attribute.location);
                    const GLenum type = to_gl_scalar(attribute.type);
                    const auto components = static_cast<GLint>(attribute.components);
                    if (is_gl_integer_scalar(attribute.type) && !attribute.normalized)
                    {
                        // Integer data the shader reads as an ivec /
                        // uvec: the float variant would convert it.
                        GL_CHECK(
                            glVertexArrayAttribIFormat(vao, attribute.location, components, type, attribute.offset));
                    }
                    else
                    {
                        GL_CHECK(glVertexArrayAttribFormat(vao,
                                                           attribute.location,
                                                           components,
                                                           type,
                                                           attribute.normalized ? GL_TRUE : GL_FALSE,
                                                           attribute.offset));
                    }
                    glVertexArrayAttribBinding(vao, attribute.location, slot);
                }
            }
        }
    } // namespace

    pipeline gl_device::create_pipeline(const pipeline_descriptor& descriptor)
    {
        gl_pipeline record{};
        record.topology = descriptor.topology;
        record.patch_control_points = descriptor.patch_control_points;
        record.blend = descriptor.blend;
        record.depth = descriptor.depth;
        record.rasterizer = descriptor.rasterizer;
        record.vertex_buffers = descriptor.vertex_buffers;
        record.bind_group_layouts = descriptor.bind_group_layouts;

        record.program_id = glCreateProgram();
        if (record.program_id == 0)
        {
            LOG_FTL("create_pipeline: glCreateProgram returned 0");
            throw std::runtime_error{"program creation failed"};
        }

        const auto attach = [&](shader_module module)
        {
            if (!module.valid())
            {
                return;
            }
            auto* shader_record = m_shader_modules.lookup(module.id);
            if (shader_record != nullptr && shader_record->object_id != 0)
            {
                glAttachShader(record.program_id, shader_record->object_id);
            }
        };
        attach(descriptor.vertex_shader);
        attach(descriptor.fragment_shader);
        attach(descriptor.geometry_shader);
        attach(descriptor.tessellation_control_shader);
        attach(descriptor.tessellation_evaluation_shader);

        glLinkProgram(record.program_id);
        check_program_link(record.program_id);

        // The VAO owns the vertex format declared by the pipeline;
        // draws only attach buffers to its binding points.
        glCreateVertexArrays(1, &record.vao_id);
        bake_vertex_format(record.vao_id, descriptor.vertex_buffers);

        record.min_strides.reserve(descriptor.vertex_buffers.size());
        for (const auto& layout : descriptor.vertex_buffers)
        {
            record.min_strides.push_back(layout_min_stride(layout));
        }
        record.vertex_binding_shadows.resize(descriptor.vertex_buffers.size());

        LOG_INF("Pipeline linked id=%u vao=%u", record.program_id, record.vao_id);

        pipeline h{};
        h.id = m_pipelines.insert(record);
        return h;
    }

    pipeline gl_device::create_compute_pipeline(const compute_pipeline_descriptor& descriptor)
    {
        gl_pipeline record{};
        record.is_compute = true;
        record.bind_group_layouts = descriptor.bind_group_layouts;

        record.program_id = glCreateProgram();
        if (record.program_id == 0)
        {
            LOG_FTL("create_compute_pipeline: glCreateProgram returned 0");
            throw std::runtime_error{"program creation failed"};
        }

        if (!descriptor.compute_shader.valid())
        {
            LOG_FTL("create_compute_pipeline: compute_shader is required");
            glDeleteProgram(record.program_id);
            throw std::runtime_error{"compute_shader missing"};
        }
        auto* shader_record = m_shader_modules.lookup(descriptor.compute_shader.id);
        if (shader_record == nullptr || shader_record->object_id == 0)
        {
            LOG_FTL("create_compute_pipeline: invalid compute shader handle");
            glDeleteProgram(record.program_id);
            throw std::runtime_error{"invalid compute shader handle"};
        }
        glAttachShader(record.program_id, shader_record->object_id);

        glLinkProgram(record.program_id);
        check_program_link(record.program_id);

        LOG_INF("Compute pipeline linked id=%u", record.program_id);

        pipeline h{};
        h.id = m_pipelines.insert(record);
        return h;
    }

    void gl_device::destroy(pipeline handle)
    {
        if (auto* record = m_pipelines.lookup(handle.id))
        {
            if (record->program_id != 0)
            {
                glDeleteProgram(record->program_id);
            }
            if (record->vao_id != 0)
            {
                glDeleteVertexArrays(1, &record->vao_id);
            }
            m_pipelines.remove(handle.id);
            // Program and VAO names may be recycled by the next create.
            invalidate_state_cache();
        }
    }

    bind_group gl_device::create_bind_group(const bind_group_descriptor& descriptor)
    {
        gl_bind_group record{};
        record.layout = descriptor.layout;
        record.entries = descriptor.entries;
        bind_group h{};
        h.id = m_bind_groups.insert(record);
        return h;
    }

    void gl_device::destroy(bind_group handle)
    {
        m_bind_groups.remove(handle.id);
    }
} // namespace rendering_engine::gpu::backend::opengl
