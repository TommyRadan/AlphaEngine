// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

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
 * Sources may @c #include other shaders by their @ref shader_library
 * path (@c #include "include/fog.glsl"); the compiler installs an
 * includer that resolves them from the library, and enables
 * @c GL_GOOGLE_include_directive on the shader's behalf. Preprocessor
 * definitions for variants are injected through
 * @ref shader_compile_options::defines, and the backend define
 * (@ref push_constants_define) by @c create_library_shader_module for a
 * device that takes push constants.
 *
 * Compiled blobs are cached on disk keyed by a digest of the source,
 * every file it (transitively) includes, the defines, the stage and the
 * glslang version, so a launch that compiles what a previous launch
 * compiled reads the SPIR-V back instead of running glslang. The cache
 * lives under @c SDL_GetPrefPath("AlphaEngine", "AlphaEngine")/shader_cache
 * unless @c ALPHAENGINE_SHADER_CACHE names another directory or is
 * @c 0 / @c off / @c false, which disables it; @ref set_shader_cache_directory
 * overrides both. The device backends keep their pipeline caches (the
 * Vulkan @c VkPipelineCache blob, the OpenGL program binaries) in
 * @ref shader_cache_directory as well, so the same switch covers them.
 *
 * The compiler is initialised lazily on first use and torn down at
 * process exit; callers may invoke it from any thread-confined context
 * the engine already runs on (today: the main thread).
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu
{
    /** @brief Preprocessor definitions, each a (name, value) pair; an empty value defines a flag. */
    using shader_defines = std::vector<std::pair<std::string, std::string>>;

    /**
     * @brief The backend define: a flag set in every library shader
     *        @c create_library_shader_module compiles for a device with
     *        @c device_features::push_constants (the Vulkan backend).
     *
     * Every stage is compiled with Vulkan semantics, but the OpenGL
     * backend loads SPIR-V through ARB_gl_spirv, which has no push
     * constants, so a shader declares a @c push_constant block only under
     * this define and falls back to a uniform buffer without it (see
     * @c shaders/include/per_draw.glsl). Being a define, it is part of the
     * SPIR-V cache key, so the two builds of a shader never collide.
     */
    inline constexpr std::string_view push_constants_define = "AE_PUSH_CONSTANTS";

    /** @brief Options for @ref compile_glsl_to_spirv. */
    struct shader_compile_options
    {
        /** @brief Definitions injected ahead of the source as @c #define name value. */
        shader_defines defines;

        /**
         * @brief Name used in diagnostics (and glslang's @c #line
         *        bookkeeping), normally the shader's library path.
         *        Empty reads as @c <inline>.
         */
        std::string_view name;
    };

    /**
     * @brief A shader-library path plus the definitions it is compiled
     *        with: the unit a material describes one of its stages in.
     */
    struct shader_variant
    {
        std::string path;
        shader_defines defines;
    };

    /**
     * @brief Compile a GLSL source string to a SPIR-V binary blob.
     *
     * @param source GLSL source. Vulkan-style @c layout(set, binding)
     *        decorations are expected on every uniform / sampler /
     *        storage binding; @c #include directives resolve against
     *        @ref shader_library.
     * @param stage Pipeline stage the source belongs to.
     * @param options Definitions to inject and the diagnostic name.
     * @return SPIR-V words, ready to hand to a backend's
     *         shader-module create call.
     * @throws std::runtime_error if parse, link, or SPIR-V emission
     *         fails. The message carries the shader name, the stage and
     *         glslang's full info log; the failure is also logged via
     *         @c LOG_ERR.
     */
    std::vector<uint32_t>
    compile_glsl_to_spirv(std::string_view source, shader_stage stage, const shader_compile_options& options);

    /** @brief @ref compile_glsl_to_spirv with default options. */
    std::vector<uint32_t> compile_glsl_to_spirv(std::string_view source, shader_stage stage);

    /**
     * @brief Compile the library shader at @p path, which doubles as the
     *        diagnostic name.
     * @throws std::runtime_error if the path is unknown or the compile fails.
     */
    std::vector<uint32_t>
    compile_library_shader(std::string_view path, shader_stage stage, const shader_defines& defines = {});

    /** @brief @ref compile_library_shader for a @ref shader_variant. */
    std::vector<uint32_t> compile_library_shader(const shader_variant& variant, shader_stage stage);

    /**
     * @brief The library files a compile of @p path reads: @p path itself,
     *        then every file it (transitively) @c #includes, each once.
     *
     * Found by the same textual scan the cache key uses, so it may list a
     * file an @c #if leaves out but never misses one the preprocessor
     * would pull in: a conservative answer to "does an edit of this file
     * affect that shader", which is what the debug hot reload asks.
     */
    std::vector<std::string> shader_dependencies(std::string_view path);

    /**
     * @brief Point the SPIR-V cache at @p directory (created on demand),
     *        or disable it with an empty path. Takes precedence over the
     *        environment variable and the default location.
     */
    void set_shader_cache_directory(const std::filesystem::path& directory);

    /**
     * @brief The SPIR-V cache directory in effect, resolved on first use;
     *        empty when the cache is disabled.
     */
    const std::filesystem::path& shader_cache_directory();

    /** @brief Cache traffic since process start. */
    struct shader_cache_stats
    {
        /** @brief Compiles served from a cached blob. */
        uint32_t hits{0};
        /** @brief Compiles that ran glslang (including every compile while the cache is disabled). */
        uint32_t misses{0};
    };

    shader_cache_stats shader_cache_statistics();
} // namespace rendering_engine::gpu
