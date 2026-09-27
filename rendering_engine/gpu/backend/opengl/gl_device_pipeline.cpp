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
 *        objects, bind-group layouts, and bind groups, and link the
 *        programs behind them (through the program-binary cache).
 */

#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

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
        // The driver's info log of @p program_id after a failed link.
        std::string program_link_log(GLuint program_id)
        {
            GLint log_length = 0;
            glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &log_length);
            std::string info_log(log_length > 0 ? static_cast<size_t>(log_length) : 1u, '\0');
            if (log_length > 0)
            {
                glGetProgramInfoLog(program_id, log_length, nullptr, info_log.data());
            }
            return info_log;
        }

        // Append @p module to the program's module list and its stage
        // (object + SPIR-V digest, what the link attaches and the
        // program-binary cache keys) to @p stages. An invalid or dead
        // handle attaches nothing.
        void append_program_stage(shader_module module,
                                  handle_pool<gl_shader_module>& modules,
                                  std::vector<shader_module>& handles,
                                  std::vector<gl_program_stage>& stages)
        {
            if (!module.valid())
            {
                return;
            }
            const gl_shader_module* record = modules.lookup(module.id);
            if (record == nullptr || record->object_id == 0)
            {
                return;
            }
            handles.push_back(module);
            stages.push_back(gl_program_stage{record->stage, record->object_id, record->spirv_digest});
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
        record.attachment_blend = descriptor.attachment_blend;
        record.depth = descriptor.depth;
        record.stencil = descriptor.stencil;
        record.depth_bias = descriptor.depth_bias;
        record.rasterizer = descriptor.rasterizer;
        record.sample_count = descriptor.sample_count;
        record.vertex_buffers = descriptor.vertex_buffers;
        record.bind_group_layouts = descriptor.bind_group_layouts;

        // Every live stage the descriptor names, in attach order.
        std::vector<gl_program_stage> stages;
        append_program_stage(descriptor.vertex_shader, m_shader_modules, record.shader_modules, stages);
        append_program_stage(descriptor.fragment_shader, m_shader_modules, record.shader_modules, stages);
        append_program_stage(descriptor.geometry_shader, m_shader_modules, record.shader_modules, stages);
        append_program_stage(descriptor.tessellation_control_shader, m_shader_modules, record.shader_modules, stages);
        append_program_stage(
            descriptor.tessellation_evaluation_shader, m_shader_modules, record.shader_modules, stages);

        std::string error;
        bool from_cache = false;
        record.program_id = link_program(stages, error, from_cache);
        if (record.program_id == 0)
        {
            throw std::runtime_error{error};
        }

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

        LOG_INF("Pipeline linked id=%u vao=%u%s",
                record.program_id,
                record.vao_id,
                from_cache ? " (from the program binary cache)" : "");

        pipeline h{};
        h.id = m_pipelines.insert(record);
        return h;
    }

    pipeline gl_device::create_compute_pipeline(const compute_pipeline_descriptor& descriptor)
    {
        gl_pipeline record{};
        record.is_compute = true;
        record.bind_group_layouts = descriptor.bind_group_layouts;

        if (!descriptor.compute_shader.valid())
        {
            LOG_FTL("create_compute_pipeline: compute_shader is required");
            throw std::runtime_error{"compute_shader missing"};
        }
        std::vector<gl_program_stage> stages;
        append_program_stage(descriptor.compute_shader, m_shader_modules, record.shader_modules, stages);
        if (stages.empty())
        {
            LOG_FTL("create_compute_pipeline: invalid compute shader handle");
            throw std::runtime_error{"invalid compute shader handle"};
        }

        std::string error;
        bool from_cache = false;
        record.program_id = link_program(stages, error, from_cache);
        if (record.program_id == 0)
        {
            throw std::runtime_error{error};
        }

        LOG_INF(
            "Compute pipeline linked id=%u%s", record.program_id, from_cache ? " (from the program binary cache)" : "");

        pipeline h{};
        h.id = m_pipelines.insert(record);
        return h;
    }

    GLuint gl_device::link_program(const std::vector<gl_program_stage>& stages, std::string& error, bool& from_cache)
    {
        from_cache = false;
        const bool cached = m_program_cache.enabled();
        const uint64_t key = cached ? m_program_cache.program_key(stages) : 0;

        // A binary an earlier run stored for exactly these stages on this
        // driver skips the link. A refused binary leaves the program
        // object unlinked, so it is dropped and the link below starts
        // from a fresh one.
        if (cached)
        {
            const GLuint program = glCreateProgram();
            if (program != 0)
            {
                if (m_program_cache.load(program, key))
                {
                    from_cache = true;
                    return program;
                }
                glDeleteProgram(program);
            }
        }

        const GLuint program = glCreateProgram();
        if (program == 0)
        {
            error = "glCreateProgram returned 0";
            LOG_ERR("Program creation failed: %s", error.c_str());
            return 0;
        }
        if (cached)
        {
            // Lets the driver keep what glGetProgramBinary needs after
            // the link.
            glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
        }
        for (const gl_program_stage& stage : stages)
        {
            glAttachShader(program, stage.object);
        }
        glLinkProgram(program);

        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE)
        {
            error = program_link_log(program);
            LOG_ERR("Program link failed: %s", error.c_str());
            glDeleteProgram(program);
            return 0;
        }
        if (cached)
        {
            m_program_cache.store(program, key);
        }
        return program;
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

        // Resolve which bind-time dynamic offset each entry takes: the
        // layout's dynamic uniform-buffer slots consume the offsets in
        // ascending binding order, as Vulkan's pDynamicOffsets does.
        record.dynamic_index.assign(record.entries.size(), gl_bind_group::no_dynamic_offset);
        if (const auto* layout = lookup_bind_group_layout(descriptor.layout))
        {
            const auto is_dynamic = [](const bind_group_layout_entry& entry)
            { return entry.kind == binding_kind::uniform_buffer && entry.has_dynamic_offset; };
            for (const auto& entry : layout->descriptor.entries)
            {
                if (is_dynamic(entry))
                {
                    ++record.dynamic_count;
                }
            }
            for (size_t i = 0; i < record.entries.size(); ++i)
            {
                const binding_value& value = record.entries[i];
                if (value.kind != binding_kind::uniform_buffer)
                {
                    continue;
                }
                uint32_t rank = 0;
                bool dynamic = false;
                for (const auto& entry : layout->descriptor.entries)
                {
                    if (!is_dynamic(entry))
                    {
                        continue;
                    }
                    if (entry.binding == value.binding)
                    {
                        dynamic = true;
                    }
                    else if (entry.binding < value.binding)
                    {
                        ++rank;
                    }
                }
                if (dynamic)
                {
                    record.dynamic_index[i] = rank;
                    if (value.size == 0)
                    {
                        LOG_WRN("create_bind_group: dynamic uniform buffer at binding %u has no size; each bind "
                                "exposes the rest of the buffer from its offset",
                                value.binding);
                    }
                }
            }
        }

        bind_group h{};
        h.id = m_bind_groups.insert(record);
        return h;
    }

    void gl_device::destroy(bind_group handle)
    {
        m_bind_groups.remove(handle.id);
    }
} // namespace rendering_engine::gpu::backend::opengl
