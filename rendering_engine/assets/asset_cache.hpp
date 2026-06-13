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
 * @file asset_cache.hpp
 * @brief Deduplicating, reference-counted store for textures, meshes and fonts.
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <rendering_engine/assets/font_asset.hpp>
#include <rendering_engine/assets/mesh_asset.hpp>
#include <rendering_engine/assets/texture_asset.hpp>

namespace rendering_engine
{
    /**
     * @brief Owns the engine's shared asset registry.
     *
     * Owned by @ref runtime::engine and brought up with the same
     * @ref init / @ref quit shape as the other subsystems. Each loader returns
     * a @c std::shared_ptr to the asset; the cache itself keeps only a matching
     * @c std::weak_ptr keyed by a normalized identity (file path, or a
     * caller-supplied structural key for procedural meshes). A second request
     * for a key whose asset is still alive returns the same handle — so a given
     * image is decoded and uploaded once, and identical geometry is uploaded
     * once — while the underlying GPU resource is released by the asset's own
     * destructor as soon as the last handle drops.
     *
     * Path-based mesh loading is intentionally absent: the engine has no mesh
     * importer yet, so meshes are cached by structural key via
     * @ref get_or_create_mesh. A file loader can slot in behind the same
     * @c shared_ptr / @c weak_ptr machinery once an importer lands.
     *
     * Depends on the gpu device being live, so it is initialised after the
     * renderer (which brings the device up) and torn down before it. Like the
     * rest of the engine it is main-thread-only and not synchronised.
     */
    struct asset_cache
    {
        asset_cache();

        /** @brief Initializes the asset cache subsystem. */
        void init();

        /**
         * @brief Shuts the subsystem down, clearing the cache's weak indices.
         *
         * The assets themselves are owned by the @c shared_ptr handles handed
         * out to callers, not by the cache, so clearing the maps only drops
         * bookkeeping. Any asset still referenced is freed by its holder's
         * handle (which runs the asset destructor and releases the GPU
         * resource); the engine guarantees that happens before the gpu device
         * is destroyed by tearing this subsystem and the renderer / scene graph
         * down ahead of it.
         */
        void quit();

        /**
         * @brief Returns the texture decoded from @p path, loading it on a miss.
         *
         * On a cache miss the image is decoded (via @ref util::image), a 2D
         * @c rgba8_unorm texture with a full mip chain is uploaded, and the
         * result is cached. Throws @c std::runtime_error if the file cannot be
         * decoded (propagated from @ref util::image).
         */
        std::shared_ptr<texture_asset> load_texture(const std::filesystem::path& path);

        /**
         * @brief Returns the font for @p path at @p size, loading it on a miss.
         *
         * Keyed on the pair @c (path, size): the same face at two sizes is two
         * assets, since glyph rasterization is size-specific.
         */
        std::shared_ptr<font_asset> load_font(const std::filesystem::path& path, float size);

        /**
         * @brief Returns the mesh named by @p key, building it on a miss.
         *
         * @p key is a caller-chosen structural identity (e.g. @c "sphere:32x16"
         * or @c "unit_cube") that uniquely describes the geometry @p builder
         * would produce. @p builder is invoked only on a miss; on a hit the
         * cached upload is shared and @p builder is never called. This is how
         * many renderables sharing identical procedural geometry collapse to a
         * single GPU upload.
         */
        std::shared_ptr<mesh_asset> get_or_create_mesh(const std::string& key,
                                                       const std::function<mesh_data()>& builder);

        /**
         * @brief Drops cache entries whose asset is no longer referenced.
         *
         * Sweeps the weak indices and erases any whose @c shared_ptr count has
         * reached zero, returning the number of entries removed. The GPU
         * resources were already released when those handles dropped; this only
         * reclaims the map slots. Cheap to call periodically (e.g. on scene
         * teardown) to keep the indices from growing unboundedly.
         */
        std::size_t collect_unused();

        /** @brief Number of textures with at least one live handle. */
        std::size_t texture_count() const;
        /** @brief Number of fonts with at least one live handle. */
        std::size_t font_count() const;
        /** @brief Number of meshes with at least one live handle. */
        std::size_t mesh_count() const;

    private:
        std::unordered_map<std::string, std::weak_ptr<texture_asset>> m_textures;
        std::unordered_map<std::string, std::weak_ptr<font_asset>> m_fonts;
        std::unordered_map<std::string, std::weak_ptr<mesh_asset>> m_meshes;
    };
} // namespace rendering_engine
