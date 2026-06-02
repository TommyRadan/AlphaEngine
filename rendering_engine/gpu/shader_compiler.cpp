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

#include <rendering_engine/gpu/shader_compiler.hpp>

#include <stdexcept>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <core/log.hpp>

namespace rendering_engine::gpu
{
    namespace
    {
        // glslang requires a single process-wide init/finalize bracket.
        // The first compile triggers initialisation; the static dtor
        // tears it down at process exit. Cheaper than wiring this into
        // engine init/quit and avoids a module-ordering dependency.
        struct glslang_process_guard
        {
            glslang_process_guard()
            {
                glslang::InitializeProcess();
            }
            ~glslang_process_guard()
            {
                glslang::FinalizeProcess();
            }
        };

        void ensure_glslang_initialized()
        {
            static glslang_process_guard guard;
            (void)guard;
        }

        EShLanguage to_glslang_stage(shader_stage stage)
        {
            switch (stage)
            {
            case shader_stage::vertex:
                return EShLangVertex;
            case shader_stage::fragment:
                return EShLangFragment;
            case shader_stage::geometry:
                return EShLangGeometry;
            case shader_stage::tessellation_control:
                return EShLangTessControl;
            case shader_stage::tessellation_evaluation:
                return EShLangTessEvaluation;
            case shader_stage::compute:
                return EShLangCompute;
            }
            return EShLangVertex;
        }
    } // namespace

    std::vector<uint32_t> compile_glsl_to_spirv(std::string_view source, shader_stage stage)
    {
        ensure_glslang_initialized();

        const EShLanguage glslang_stage = to_glslang_stage(stage);

        glslang::TShader shader{glslang_stage};
        const char* source_data = source.data();
        const int source_length = static_cast<int>(source.size());
        shader.setStringsWithLengths(&source_data, &source_length, 1);

        // Vulkan client + SPV target — produces SPIR-V usable both by
        // a Vulkan backend natively and by an OpenGL 4.6 backend
        // through ARB_gl_spirv. The input version (100) is the
        // glslang convention for GLSL.
        shader.setEnvInput(glslang::EShSourceGlsl, glslang_stage, glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

        const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);

        if (!shader.parse(GetDefaultResources(), 100, false, messages))
        {
            LOG_ERR("glslang parse failed (stage=%i): %s\n%s",
                    static_cast<int>(stage),
                    shader.getInfoLog(),
                    shader.getInfoDebugLog());
            throw std::runtime_error{"glslang shader parse failed"};
        }

        glslang::TProgram program;
        program.addShader(&shader);
        if (!program.link(messages))
        {
            LOG_ERR("glslang link failed (stage=%i): %s\n%s",
                    static_cast<int>(stage),
                    program.getInfoLog(),
                    program.getInfoDebugLog());
            throw std::runtime_error{"glslang program link failed"};
        }

        std::vector<uint32_t> spirv;
        glslang::SpvOptions options{};
        options.disableOptimizer = true;
        glslang::GlslangToSpv(*program.getIntermediate(glslang_stage), spirv, &options);

        if (spirv.empty())
        {
            LOG_ERR("glslang produced empty SPIR-V (stage=%i)", static_cast<int>(stage));
            throw std::runtime_error{"glslang produced empty SPIR-V"};
        }

        return spirv;
    }
} // namespace rendering_engine::gpu
