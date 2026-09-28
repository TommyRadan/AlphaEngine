// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file shader_library.hpp
 * @brief The engine's GLSL sources and the game's shader assets, looked
 *        up by path.
 *
 * A shader is named by its path relative to a @c shaders/ directory,
 * forward slashes, e.g. @c "materials/standard.frag.glsl" or
 * @c "include/fog.glsl". The same names are what a shader's @c #include
 * directive resolves against, so a shader includes
 * @c "include/fog.glsl" no matter where it lives. A name resolves, in
 * this order, to:
 *
 * 1. an **asset shader**: the file @c shaders/<path> in the virtual
 *    filesystem (@c core::default_vfs(), which mounts the content root),
 *    in every build type. That is how a game ships shaders of its own
 *    — or replaces a built-in one — without rebuilding the engine: its
 *    material templates name them like any other shader, and they
 *    include the engine's headers (@c include/per_frame.glsl,
 *    @c include/per_draw.glsl, @c include/per_material.glsl, ...) the way
 *    the built-ins do. Only the mounts are searched, never the working
 *    directory, and each file's text is read once and kept;
 * 2. in debug builds, the **on-disk override** of an embedded shader:
 *    when an override root is set (the @c ALPHAENGINE_SHADER_DIR
 *    environment variable, else the source tree's @c shaders/ directory
 *    baked in at build time) and @c <root>/<path> exists, its contents
 *    win over the embedded copy. That is what lets a built-in shader be
 *    edited without a C++ rebuild. Release builds ignore the override
 *    and read no source-tree file;
 * 3. the **embedded registry**: every file under the repository's
 *    @c shaders/ tree that @c shaders/CMakeLists.txt lists, embedded into
 *    the binary at build time (cmake/embed_shaders.cmake generates the
 *    registry), which is the fallback for the built-ins. The generated
 *    @c include/bindings.glsl is never on disk and always comes from
 *    here unless an asset shadows it.
 *
 * Text read from a file is kept until @ref refresh drops it, which the
 * debug hot reload (shader_hot_reload.hpp) does for every file it sees
 * change — an edited override, or an asset shader whose modification
 * time moved (@ref changed_asset_paths) — so the next compile picks the
 * edit up.
 *
 * Main-thread only, like the rest of the renderer.
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rendering_engine::gpu::shader_library
{
    /**
     * @brief The virtual-filesystem directory asset shaders live in: the
     *        library path @c p resolves to the VFS file
     *        @c "shaders/<p>", @c content/shaders/<p> under the default
     *        content root.
     */
    inline constexpr std::string_view asset_directory = "shaders";

    /**
     * @brief The GLSL text of the shader at @p path (see the file comment
     *        for the order the sources are searched in).
     *
     * @param path Path relative to @c shaders/, forward slashes.
     * @return A view into the embedded registry (valid for the rest of
     *         the process) or into text read from a file: an asset
     *         shader's, or the override root's, which
     *         @ref set_override_root drops.
     * @throws std::runtime_error when no asset, on-disk or embedded
     *         shader has that path.
     */
    std::string_view source(std::string_view path);

    /** @brief Whether @ref source would succeed for @p path. */
    bool contains(std::string_view path);

    /** @brief Every embedded path, sorted; asset shaders and the override root add none. */
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
     * @brief Forget the text read from a file for @p path — the asset
     *        shader's and the override root's — so the next lookup reads
     *        the file again (or falls back to the next source when it is
     *        gone). Debug builds only; a no-op in release builds and for a
     *        path whose text was never read. Views returned earlier stay
     *        valid: the old text is retired, not freed.
     */
    void refresh(std::string_view path);

    /**
     * @brief The asset shaders read so far whose file changed since:
     *        its modification time moved, or no mount holds it any more.
     *        Debug builds only (empty in release builds); the hot reload
     *        polls it and hands what it reports to @ref refresh.
     */
    std::vector<std::string> changed_asset_paths();

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
