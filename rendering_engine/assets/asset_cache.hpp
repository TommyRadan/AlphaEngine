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

#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rendering_engine/assets/font_asset.hpp>
#include <rendering_engine/assets/mesh_asset.hpp>
#include <rendering_engine/assets/texture_asset.hpp>
#include <rendering_engine/gpu/types.hpp>
#include <rendering_engine/util/image.hpp>

namespace core
{
    struct jobs;
}

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
     * File assets are read through the virtual filesystem
     * (@c core::default_vfs): a relative path is looked up in the mounted
     * asset root, an absolute one names a native file. Their keys are the
     * VFS's canonical identity (@c vfs::canonical_key — resolved, made
     * canonical, case-folded where the filesystem ignores case), so two
     * spellings of one file share one entry. Numeric parameters in a
     * structural key go through @c cache_key_number so the text is
     * locale-independent and distinguishes every distinct value.
     *
     * Meshes are cached by structural key via @ref get_or_create_mesh rather
     * than by path: procedural builders key on their parameters, and the glTF
     * importer (@ref load_gltf) keys each primitive on the file's canonical
     * identity plus its mesh / primitive index, so a model loaded twice shares
     * its uploads through the same @c shared_ptr / @c weak_ptr machinery.
     *
     * Textures can also be loaded asynchronously (@ref load_texture_async):
     * the file is read and decoded on the @c core::jobs worker pool and the
     * handle returned at once carries the cache's placeholder texture until
     * @ref pump, called once per frame from the main thread, performs the
     * device upload. A decode failure is logged and leaves the placeholder in
     * place — nothing on that path throws into the frame.
     *
     * Depends on the gpu device being live, so it is initialised after the
     * renderer (which brings the device up) and torn down before it. The
     * loaders and @ref pump are main-thread calls; the indices are guarded by
     * a shared mutex so the counters and sweeps are safe from any thread,
     * and the worker side of an asynchronous load touches only its own
     * pending record.
     */
    struct asset_cache
    {
        asset_cache();

        /** @brief Waits for any in-flight asynchronous decode, then drops the indices. */
        ~asset_cache();

        asset_cache(const asset_cache&) = delete;
        asset_cache& operator=(const asset_cache&) = delete;

        /** @brief Initializes the asset cache subsystem. */
        void init();

        /**
         * @brief Shuts the subsystem down: finishes (and discards) any
         *        in-flight asynchronous load, releases the placeholder
         *        texture and clears the cache's weak indices.
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
         * @brief Installs the worker pool asynchronous loads decode on. With
         *        none installed (or @c nullptr) the decode runs inline on
         *        the caller and still completes through @ref pump.
         */
        void set_jobs(core::jobs* jobs);

        /**
         * @brief Returns the texture decoded from @p path, loading it on a miss.
         *
         * On a cache miss the image is read through the VFS and decoded (via
         * @ref util::image), a 2D RGBA8 texture with a full mip chain is
         * uploaded, and the result is cached. @p space names the colour space
         * the file was authored in and selects the texel format through
         * @ref gpu::rgba8_format: @c srgb (the default, right for albedo /
         * base-colour / emissive images and anything else meant for the eye)
         * uploads @c rgba8_srgb so the GPU decodes to linear on sample;
         * @c linear uploads @c rgba8_unorm for data maps (normals, metalness,
         * roughness, AO) whose bytes are already linear. The colour space is
         * part of the cache key — like a font's size — so the same file
         * requested in both spaces is two assets, never one mis-decoded one.
         * Throws @c std::runtime_error if the file cannot be decoded
         * (propagated from @ref util::image). A request for a key whose
         * asynchronous load is still in flight returns that asset, still
         * resolving.
         */
        std::shared_ptr<texture_asset> load_texture(const std::filesystem::path& path,
                                                    gpu::color_space space = gpu::color_space::srgb);

        /**
         * @brief Like @ref load_texture, but reads and decodes on the worker
         *        pool and returns immediately.
         *
         * The returned asset starts in @ref texture_asset::load_state::loading
         * with @ref texture_asset::texture set to the cache's shared 1x1
         * placeholder, so it can be bound at once. When the decode has landed,
         * the next @ref pump uploads it on the main thread, swaps the asset's
         * handle to its own texture and marks it @c ready; if the decode
         * failed, @ref pump logs the error and marks it @c failed, keeping
         * the placeholder. Never throws for a missing or corrupt file. A hit
         * on a live entry returns that asset whatever its state.
         */
        std::shared_ptr<texture_asset> load_texture_async(const std::filesystem::path& path,
                                                          gpu::color_space space = gpu::color_space::srgb);

        /**
         * @brief Completes the asynchronous loads whose decodes have finished:
         *        uploads each to the device and resolves its asset. Call once
         *        per frame from the main thread (the engine does, from
         *        @c engine::tick). Every @c k_sweep_interval calls it also
         *        runs @ref collect_unused.
         * @return The number of loads resolved (successfully or not) by this call.
         */
        std::size_t pump();

        /** @brief Blocks until every in-flight decode has finished (they still resolve through @ref pump). */
        void wait_pending();

        /** @brief Number of asynchronous loads not yet resolved by @ref pump. */
        std::size_t pending_count() const;

        /**
         * @brief The 1x1 mid-grey texture asynchronous assets stand in on,
         *        created on first use. Owned by the cache; released in
         *        @ref quit.
         */
        std::shared_ptr<texture_asset> placeholder_texture();

        /**
         * @brief Returns the texture for an already-decoded @p image under
         *        @p key, uploading it on a miss.
         *
         * For images that have no file of their own — a PNG embedded in a
         * GLB chunk or a data URI — where @ref load_texture cannot key on a
         * path. @p key is a caller-chosen identity that uniquely names the
         * pixels (the glTF importer uses the model's canonical identity plus
         * the image index); it lives in the same index as the path keys, so
         * pick one a path can never spell. @p image is read only on a miss
         * and never retained: the upload copies the pixels, so the caller may
         * drop the decoded image as soon as this returns. @p space selects
         * the texel format and is part of the key exactly as in
         * @ref load_texture.
         */
        std::shared_ptr<texture_asset> load_texture_from_image(const std::string& key,
                                                               const util::image& image,
                                                               gpu::color_space space = gpu::color_space::srgb);

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
         * single GPU upload. A builder that produces no geometry (no vertex
         * bytes or a zero stride) is an error: it is logged, nothing is
         * uploaded or cached, and @c nullptr is returned.
         */
        std::shared_ptr<mesh_asset> get_or_create_mesh(const std::string& key,
                                                       const std::function<mesh_data()>& builder);

        /**
         * @brief The live mesh cached under @p key, or @c nullptr; never
         *        builds one.
         *
         * For a caller that holds only a key (a scene file's mesh reference,
         * see @ref mesh_asset::key) and so has no builder to hand
         * @ref get_or_create_mesh.
         */
        std::shared_ptr<mesh_asset> find_mesh(const std::string& key) const;

        /**
         * @brief Drops cache entries whose asset is no longer referenced.
         *
         * Sweeps the weak indices and erases any whose @c shared_ptr count has
         * reached zero, returning the number of entries removed. The GPU
         * resources were already released when those handles dropped; this only
         * reclaims the map slots. The engine calls it at scene teardown, and
         * @ref pump every @c k_sweep_interval frames, so the indices never
         * grow without bound.
         */
        std::size_t collect_unused();

        /** @brief How many @ref pump calls pass between automatic @ref collect_unused sweeps. */
        static constexpr std::size_t k_sweep_interval = 300;

        /** @brief A snapshot of the index sizes; see @ref statistics. */
        struct stats
        {
            std::size_t textures{0};        /**< Texture entries with at least one live handle. */
            std::size_t texture_entries{0}; /**< Texture entries in the index, expired ones included. */
            std::size_t fonts{0};           /**< Font entries with at least one live handle. */
            std::size_t font_entries{0};    /**< Font entries in the index, expired ones included. */
            std::size_t meshes{0};          /**< Mesh entries with at least one live handle. */
            std::size_t mesh_entries{0};    /**< Mesh entries in the index, expired ones included. */
            std::size_t pending_loads{0};   /**< Asynchronous loads not yet resolved by @ref pump. */
        };

        /**
         * @brief Live counts next to raw index sizes: the difference is what
         *        @ref collect_unused would reclaim.
         */
        stats statistics() const;

        /** @brief Number of textures with at least one live handle (see @ref statistics for the index size). */
        std::size_t texture_count() const;
        /** @brief Number of fonts with at least one live handle (see @ref statistics for the index size). */
        std::size_t font_count() const;
        /** @brief Number of meshes with at least one live handle (see @ref statistics for the index size). */
        std::size_t mesh_count() const;

    private:
        // One asynchronous texture load, shared between the worker that
        // decodes it and the main thread that uploads it. The worker writes
        // `image` / `error` and then `done`, under `asset_cache::m_pending_mutex`;
        // pump() reads them back under the same mutex once `done` is set.
        struct pending_texture
        {
            std::shared_ptr<texture_asset> asset;
            std::filesystem::path path;
            std::string label; // the path as given, for the log
            gpu::color_space space{gpu::color_space::srgb};
            util::image image;
            std::string error;
            bool done{false};
        };

        // Runs the decode of @p job (on whichever thread) and flags it done.
        void decode_pending(const std::shared_ptr<pending_texture>& job);

        // Guards the three indices.
        mutable std::shared_mutex m_mutex;
        std::unordered_map<std::string, std::weak_ptr<texture_asset>> m_textures;
        std::unordered_map<std::string, std::weak_ptr<font_asset>> m_fonts;
        std::unordered_map<std::string, std::weak_ptr<mesh_asset>> m_meshes;

        // Asynchronous loading.
        core::jobs* m_jobs{nullptr};
        std::shared_ptr<texture_asset> m_placeholder;
        mutable std::mutex m_pending_mutex;
        std::condition_variable m_pending_changed;
        std::vector<std::shared_ptr<pending_texture>> m_pending;
        std::size_t m_pump_count{0};
    };
} // namespace rendering_engine
