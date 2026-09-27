// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file shader_library.hpp
 * @brief The engine's GLSL sources, looked up by path.
 *
 * Every file under the repository's @c shaders/ tree that
 * @c shaders/CMakeLists.txt lists is embedded into the binary at build
 * time (cmake/embed_shaders.cmake generates the registry) and served
 * here by its path relative to @c shaders/, forward slashes, e.g.
 * @c "materials/standard.frag.glsl" or @c "include/fog.glsl". The same
 * names are what a shader's @c #include directive resolves against, so
 * a shader includes @c "include/fog.glsl" no matter where it lives.
 *
 * Debug builds can additionally read shaders from disk: when an
 * override root is set (the @c ALPHAENGINE_SHADER_DIR environment
 * variable, else the source tree's @c shaders/ directory baked in at
 * build time) and @c <root>/<path> exists, its contents win over the
 * embedded copy. That is what lets a shader be edited without a C++
 * rebuild; the file is read once and kept until @ref refresh drops it,
 * which the debug hot reload (shader_hot_reload.hpp) does for every file
 * it sees change, so the next compile picks the edit up. The
 * generated @c include/bindings.glsl is never on disk and always comes
 * from the embedded registry. Release builds ignore the override and
 * touch no files.
 *
 * Main-thread only, like the rest of the renderer.
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

namespace rendering_engine::gpu::shader_library
{
    /**
     * @brief The GLSL text of the shader at @p path.
     *
     * @param path Path relative to @c shaders/, forward slashes.
     * @return A view into the embedded registry (valid for the rest of
     *         the process) or into the on-disk text read for the
     *         override root, which @ref set_override_root drops.
     * @throws std::runtime_error when no embedded or on-disk shader has
     *         that path.
     */
    std::string_view source(std::string_view path);

    /** @brief Whether @ref source would succeed for @p path. */
    bool contains(std::string_view path);

    /** @brief Every embedded path, sorted; the override root adds none. */
    std::vector<std::string_view> embedded_paths();

    /**
     * @brief Set (or, with an empty path, clear) the on-disk override
     *        root for this process. Debug builds only; a no-op in
     *        release builds.
     */
    void set_override_root(const std::filesystem::path& root);

    /**
     * @brief The on-disk override root in effect, empty when overrides
     *        are off (always empty in release builds).
     */
    const std::filesystem::path& override_root();

    /**
     * @brief Forget the on-disk text read for @p path, so the next
     *        lookup reads the file again (or falls back to the embedded
     *        copy when it is gone). Debug builds only; a no-op in release
     *        builds and for a path whose text was never read. Views
     *        returned earlier stay valid: the old text is retired, not
     *        freed.
     */
    void refresh(std::string_view path);

    namespace detail
    {
        /** @brief One embedded shader: its registry key and its text. */
        struct shader_registry_entry
        {
            const char* path;
            const char* source;
            std::size_t size;
        };

        /**
         * @brief The embedded registry, implemented by the generated
         *        shader_registry.cpp. @p count receives the entry count.
         */
        const shader_registry_entry* embedded_shaders(std::size_t& count);
    } // namespace detail
} // namespace rendering_engine::gpu::shader_library
