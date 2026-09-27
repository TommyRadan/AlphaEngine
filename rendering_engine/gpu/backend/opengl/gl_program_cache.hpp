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
 * @file gl_program_cache.hpp
 * @brief The OpenGL backend's on-disk cache of linked program binaries.
 *
 * Linking a program is where an OpenGL driver does most of its shader
 * compilation. When the driver offers at least one program-binary format
 * (@c GL_NUM_PROGRAM_BINARY_FORMATS) and the shader cache directory is
 * enabled (@ref gpu::shader_cache_directory, so @c ALPHAENGINE_SHADER_CACHE
 * turns it off or moves it like the SPIR-V cache), @c gl_device links
 * every program through this cache: it first offers the driver the blob
 * @c glGetProgramBinary returned for the same program on an earlier run
 * (@c glProgramBinary), and only when there is none, or the driver
 * refuses it (@c GL_LINK_STATUS false — a driver update, a different
 * GPU), attaches the shader objects and links as usual, then stores the
 * new binary. Callers see a linked program either way.
 *
 * A blob is keyed on the driver identity (@c GL_VENDOR, @c GL_RENDERER and
 * @c GL_VERSION) plus, per attached stage, its stage and a digest of the
 * SPIR-V it was specialised from and of the specialisation (entry point
 * "main", no constants). Each file carries a small header — magic,
 * version, the key, the binary format, the length and an FNV-1a digest
 * of the binary — so a truncated file, a key collision or a format the
 * driver no longer lists is a miss rather than a blob handed to the
 * driver. Files are written through a temporary and a rename.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <glad/gl.h>

#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    // One stage of a program as the cache keys it and the linker
    // attaches it.
    struct gl_program_stage
    {
        shader_stage stage{shader_stage::vertex};
        GLuint object{0};
        uint64_t spirv_digest{0};
    };

    struct gl_program_cache
    {
        // Read the driver identity and its binary formats and decide
        // whether the cache is on; logs the outcome. Needs the current
        // context.
        void init();

        // Log the run's traffic and turn the cache off.
        void quit();

        bool enabled() const noexcept;

        // The key of a program linked from @p stages, in attach order.
        uint64_t program_key(const std::vector<gl_program_stage>& stages) const;

        // Load the binary stored under @p key into @p program, a fresh
        // program object. True when the driver accepted it (the program
        // is then linked); false when there is no usable blob or the
        // driver refused it, in which case the caller should discard
        // @p program and link a new one from its shader objects.
        bool load(GLuint program, uint64_t key);

        // Store the binary of @p program, freshly linked with
        // @c GL_PROGRAM_BINARY_RETRIEVABLE_HINT, under @p key. A failure
        // only costs a relink next run, so it is logged, not thrown.
        void store(GLuint program, uint64_t key);

    private:
        std::filesystem::path file_for(uint64_t key) const;

        bool m_enabled{false};
        std::filesystem::path m_directory;
        // FNV-1a of "GL_VENDOR|GL_RENDERER|GL_VERSION".
        uint64_t m_driver_key{0};
        // The formats GL_PROGRAM_BINARY_FORMATS lists; a stored blob in
        // any other format is never offered to the driver.
        std::vector<GLint> m_formats;
        uint32_t m_hits{0};
        uint32_t m_misses{0};
        uint32_t m_refused{0};
        uint32_t m_stored{0};
    };
} // namespace rendering_engine::gpu::backend::opengl
