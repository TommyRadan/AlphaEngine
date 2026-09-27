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
 * @file gl_device_shader.cpp
 * @brief @c gl_device member functions that compile shader stages:
 *        @c create_shader_module, @c destroy(shader_module), and the
 *        debug hot reload that swaps new code in behind a module.
 *
 * Shader modules consume SPIR-V byte blobs produced upstream by
 * @ref rendering_engine::gpu::compile_glsl_to_spirv. The blob is
 * uploaded via @c glShaderBinary with the
 * @c GL_SHADER_BINARY_FORMAT_SPIR_V format and specialised into the
 * shader object via @c glSpecializeShader (core in OpenGL 4.6 — the
 * ARB_gl_spirv path). No GLSL source ever reaches the driver from
 * this layer.
 */

#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glad/gl.h>

#include <core/hash.hpp>
#include <core/log.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_check.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_translate.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    namespace
    {
        // Upload @p spirv into @p object and specialise it (entry point
        // "main", no specialisation constants — what the program-binary
        // cache key assumes). Returns false with the driver's log in
        // @p info_log when the driver rejects it.
        bool specialize_spirv(GLuint object, const std::vector<uint32_t>& spirv, std::string& info_log)
        {
            const GLsizei blob_bytes = static_cast<GLsizei>(spirv.size() * sizeof(uint32_t));
            // A rejected blob (bad format enum, truncated module) raises a
            // GL error without touching the compile status checked below.
            GL_CHECK(glShaderBinary(1, &object, GL_SHADER_BINARY_FORMAT_SPIR_V, spirv.data(), blob_bytes));
            glSpecializeShader(object, "main", 0, nullptr, nullptr);

            GLint compiled = 0;
            glGetShaderiv(object, GL_COMPILE_STATUS, &compiled);
            if (compiled == GL_TRUE)
            {
                return true;
            }
            GLint log_length = 0;
            glGetShaderiv(object, GL_INFO_LOG_LENGTH, &log_length);
            info_log.assign(log_length > 0 ? static_cast<size_t>(log_length) : 1u, '\0');
            if (log_length > 0)
            {
                glGetShaderInfoLog(object, log_length, nullptr, info_log.data());
            }
            return false;
        }

        uint64_t spirv_digest(const std::vector<uint32_t>& spirv)
        {
            return core::fnv1a_64(
                std::string_view{reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(uint32_t)});
        }
    } // namespace

    shader_module gl_device::create_shader_module(const shader_module_descriptor& descriptor)
    {
        if (descriptor.spirv.empty())
        {
            LOG_FTL("create_shader_module: SPIR-V blob is empty");
            throw std::runtime_error{"empty SPIR-V blob"};
        }

        gl_shader_module record{};
        record.stage = descriptor.stage;
        record.object_id = glCreateShader(to_gl_shader_stage(descriptor.stage));
        if (record.object_id == 0)
        {
            LOG_FTL("create_shader_module: glCreateShader returned 0");
            throw std::runtime_error{"shader creation failed"};
        }

        std::string info_log;
        if (!specialize_spirv(record.object_id, descriptor.spirv, info_log))
        {
            LOG_ERR("Shader specialization failed: %s", info_log.c_str());
            glDeleteShader(record.object_id);
            throw std::runtime_error{info_log};
        }
        record.spirv_digest = spirv_digest(descriptor.spirv);

        LOG_INF("Specialized shader id=%u stage=%i (%i SPIR-V bytes)",
                record.object_id,
                static_cast<int>(descriptor.stage),
                static_cast<int>(descriptor.spirv.size() * sizeof(uint32_t)));

        shader_module h{};
        h.id = m_shader_modules.insert(record);
        return h;
    }

    void gl_device::destroy(shader_module handle)
    {
        if (auto* record = m_shader_modules.lookup(handle.id))
        {
            if (record->object_id != 0)
            {
                glDeleteShader(record->object_id);
            }
            m_shader_modules.remove(handle.id);
        }
    }

#if defined(_DEBUG)
    bool gl_device::shader_module_live(shader_module module)
    {
        return m_shader_modules.lookup(module.id) != nullptr;
    }

    bool gl_device::reload_shader_modules(const std::vector<shader_module_update>& updates)
    {
        if (!m_initialised)
        {
            return false;
        }

        // 1. A new shader object for every live module of the batch (a
        //    module its owner destroyed meanwhile is skipped).
        struct staged_shader
        {
            shader_module handle{};
            GLuint object{0};
            uint64_t digest{0};
        };
        std::vector<staged_shader> staged;
        const auto find_staged = [&staged](shader_module handle) -> const staged_shader*
        {
            for (const staged_shader& shader : staged)
            {
                if (handle.valid() && shader.handle == handle)
                {
                    return &shader;
                }
            }
            return nullptr;
        };
        const auto discard_new_shaders = [&staged]
        {
            for (const staged_shader& shader : staged)
            {
                glDeleteShader(shader.object);
            }
        };
        for (const shader_module_update& update : updates)
        {
            const gl_shader_module* record = m_shader_modules.lookup(update.module.id);
            if (record == nullptr || find_staged(update.module) != nullptr)
            {
                continue;
            }
            if (update.spirv.empty())
            {
                LOG_ERR("gl_device::reload_shader_modules: empty SPIR-V");
                discard_new_shaders();
                return false;
            }
            const GLuint object = glCreateShader(to_gl_shader_stage(record->stage));
            std::string info_log = "glCreateShader returned 0";
            if (object == 0 || !specialize_spirv(object, update.spirv, info_log))
            {
                LOG_ERR("Shader specialization failed: %s", info_log.c_str());
                if (object != 0)
                {
                    glDeleteShader(object);
                }
                discard_new_shaders();
                return false;
            }
            staged.push_back(staged_shader{update.module, object, spirv_digest(update.spirv)});
        }
        if (staged.empty())
        {
            return true;
        }

        // 2. Relink every program linked from a replaced module, with the
        //    new objects in place of the old ones and every other stage
        //    as it is. A program one of whose other stages its owner has
        //    since destroyed cannot be relinked and keeps its code.
        struct relinked_program
        {
            gl_pipeline* record{nullptr};
            GLuint program{0};
        };
        std::vector<relinked_program> relinked;
        bool failed = false;
        m_pipelines.for_each(
            [&](gl_pipeline& record)
            {
                if (failed)
                {
                    return;
                }
                bool affected = false;
                for (const shader_module module : record.shader_modules)
                {
                    affected = affected || find_staged(module) != nullptr;
                }
                if (!affected)
                {
                    return;
                }
                std::vector<gl_program_stage> stages;
                for (const shader_module module : record.shader_modules)
                {
                    const gl_shader_module* current = m_shader_modules.lookup(module.id);
                    if (current == nullptr || current->object_id == 0)
                    {
                        LOG_WRN("gl_device: program %u lost one of its shader modules; it keeps its previous code",
                                record.program_id);
                        return;
                    }
                    const staged_shader* replacement = find_staged(module);
                    stages.push_back(replacement != nullptr
                                         ? gl_program_stage{current->stage, replacement->object, replacement->digest}
                                         : gl_program_stage{current->stage, current->object_id, current->spirv_digest});
                }
                std::string error;
                bool from_cache = false;
                const GLuint program = link_program(stages, error, from_cache);
                if (program == 0)
                {
                    failed = true;
                    return;
                }
                relinked.push_back(relinked_program{&record, program});
            });
        if (failed)
        {
            for (const relinked_program& entry : relinked)
            {
                glDeleteProgram(entry.program);
            }
            discard_new_shaders();
            LOG_WRN("gl_device: a program failed to relink with the reloaded shaders; the previous ones stay");
            return false;
        }

        // 3. Commit. GL keeps a deleted program alive for as long as a
        //    submitted command still uses it, and a deleted shader for
        //    as long as a program has it attached.
        for (const staged_shader& shader : staged)
        {
            gl_shader_module* record = m_shader_modules.lookup(shader.handle.id);
            glDeleteShader(record->object_id);
            record->object_id = shader.object;
            record->spirv_digest = shader.digest;
        }
        for (const relinked_program& entry : relinked)
        {
            glDeleteProgram(entry.record->program_id);
            entry.record->program_id = entry.program;
        }
        // The shadowed current program may name a deleted one, whose
        // name GL is free to recycle.
        invalidate_state_cache();
        LOG_DBG("gl_device: hot reload replaced %zu shader module(s) and relinked %zu program(s)",
                staged.size(),
                relinked.size());
        return true;
    }
#endif
} // namespace rendering_engine::gpu::backend::opengl
