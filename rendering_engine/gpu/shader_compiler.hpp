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
 * @file shader_compiler.hpp
 * @brief GLSL-to-SPIR-V compilation entry point, backed by glslang.
 *
 * The engine authors vanilla GLSL with Vulkan-style
 * @c layout(set, binding) annotations and ships SPIR-V byte blobs.
 * Vulkan backends consume those blobs natively; OpenGL 4.6 backends
 * upload them through @c glShaderBinary + @c glSpecializeShaderARB
 * (core ARB_gl_spirv).
 *
 * The compiler is initialised lazily on first use and torn down at
 * process exit; callers may invoke @ref compile_glsl_to_spirv from
 * any thread-confined context the engine already runs on (today: the
 * main thread).
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu
{
    /**
     * @brief Compile a GLSL source string to a SPIR-V binary blob.
     *
     * @param source GLSL source. Vulkan-style
     *        @c layout(set, binding) decorations are expected on
     *        every uniform / sampler / storage binding.
     * @param stage Pipeline stage the source belongs to.
     * @return SPIR-V words, ready to hand to a backend's
     *         shader-module create call.
     * @throws std::runtime_error if parse, link, or SPIR-V emission
     *         fails; the failure is also logged via @c LOG_ERR.
     */
    std::vector<uint32_t> compile_glsl_to_spirv(std::string_view source, shader_stage stage);
} // namespace rendering_engine::gpu
