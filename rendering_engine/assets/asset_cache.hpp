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
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rendering_engine/assets/font_asset.hpp>
#include <rendering_engine/assets/image.hpp>
#include <rendering_engine/assets/mesh_asset.hpp>
#include <rendering_engine/assets/texture_asset.hpp>
#include <rendering_engine/assets/texture_decode.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace core
{
    struct job_pool;

    namespace platform
    {
        struct directory_watcher;
    }
} // namespace core

namespace rendering_engine
{
    struct gltf_asset;
    struct gltf_import_options;
    struct gltf_material_factory;

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
     * the file is read and decoded on the @c core::job_pool worker pool and the
     * handle returned at once carries the cache's placeholder texture until
     * @ref pump, called once per frame from the main thread, performs the
     * device upload. A decode failure is logged and leaves the placeholder in
     * place — nothing on that path throws into the frame. glTF models load the
     * same way (@ref load_gltf_async): the parse, image decodes and geometry
     * build run on a worker, the uploads and materials in @ref pump.
     *
     * Besides PNG / JPEG / ... (stb_image), @ref load_texture reads KTX2
     * containers through libktx, uploading their block-compressed levels as
     * they are or transcoding Basis Universal content to the best format the
     * device samples (see @ref decode_ktx2).
     *
     * With @ref enable_hot_reload (the engine turns it on in debug builds for
     * the asset root) @ref pump also polls a directory watcher: a texture
     * file that changes on disk is decoded again on a worker and its new
     * upload swapped into the live @ref texture_asset, whose
     * @c generation then moves so bound materials rebuild.
     *
     * Depends on the gpu device being live, so it is initialised after the
     * renderer (which brings the device up) and torn down before it. The
     * loaders and @ref pump are main-thread calls; the indices are guarded by
     * a shared mutex so the counters and sweeps are safe from any thread,
     * and the worker side of an asynchronous load (or reload) touches only
     * the state its own job carries.
     */
    struct asset_cache
    {
        asset_cache();

        /** @brief Waits for any in-flight asynchronous work (decode, reload, glTF import), then drops the indices. */
        ~asset_cache();

        asset_cache(const asset_cache&) = delete;
        asset_cache& operator=(const asset_cache&) = delete;

        /** @brief Initializes the asset cache subsystem. */
        void init();

        /**
         * @brief Shuts the subsystem down: stops the hot-reload watch,
         *        finishes (and discards) any in-flight asynchronous load or
         *        reload — a texture keeps the placeholder, a glTF load is
         *        marked failed — drops the installed glTF material factory,
         *        releases the placeholder texture and clears the cache's weak
         *        indices.
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
        void set_jobs(core::job_pool* jobs);

        /**
         * @brief Returns the texture decoded from @p path, loading it on a miss.
         *
         * On a cache miss the image is read through the VFS and decoded (via
         * @ref image), a 2D RGBA8 texture with a full mip chain is
         * uploaded, and the result is cached. A @c .ktx2 file is decoded by
         * @ref decode_ktx2 instead and uploaded with the levels it carries,
         * in the block-compressed format it holds or was transcoded to (the
         * colour space then picks that format's sRGB or unorm form, where it
         * has one). @p space names the colour space the file was authored
         * in and selects the texel format through
         * @ref gpu::rgba8_format: @c srgb (the default, right for albedo /
         * base-colour / emissive images and anything else meant for the eye)
         * uploads @c rgba8_srgb so the GPU decodes to linear on sample;
         * @c linear uploads @c rgba8_unorm for data maps (normals, metalness,
         * roughness, AO) whose bytes are already linear. The colour space is
         * part of the cache key — like a font's size — so the same file
         * requested in both spaces is two assets, never one mis-decoded one.
         * Throws @c std::runtime_error if the file cannot be decoded
         * (propagated from @ref image or @ref decode_ktx2) or the
         * device refuses the upload. A request for a key whose
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
         * handle to its own texture (moving its @c generation) and marks it
         * @c ready; if the decode
         * failed, @ref pump logs the error and marks it @c failed, keeping
         * the placeholder. Never throws for a missing or corrupt file. A hit
         * on a live entry returns that asset whatever its state.
         */
        std::shared_ptr<texture_asset> load_texture_async(const std::filesystem::path& path,
                                                          gpu::color_space space = gpu::color_space::srgb);

        /**
         * @brief Completes the asynchronous work whose worker half has
         *        finished: uploads each decoded texture and resolves its
         *        asset, swaps each hot-reloaded texture in, and finishes each
         *        glTF load. Call once per frame from the main thread (the
         *        engine does, from @c engine::tick). Every
         *        @c k_sweep_interval calls it also runs @ref collect_unused,
         *        and with @ref enable_hot_reload it polls the watched
         *        directory every @c k_hot_reload_interval_ms.
         * @return The number of loads resolved (successfully or not) by this call.
         */
        std::size_t pump();

        /**
         * @brief Watches the directory tree under @p root and, from then on,
         *        reloads every live texture whose file under it changes.
         *
         * @ref pump polls a @c core::platform::directory_watcher at most
         * every @c k_hot_reload_interval_ms. A file that was modified (or
         * re-created) is matched to the cached textures by its canonical
         * identity, in both colour spaces; each live one is decoded again on
         * the worker pool and, in a later @ref pump, its new upload replaces
         * the old texture inside the same @ref texture_asset (the old one is
         * released, @c generation bumped, the reload logged). A file that
         * fails to decode — often one caught half-written — is logged and
         * leaves the previous texture in place; the next change retries. A
         * load still in flight is not reloaded, and removed files are
         * ignored. The engine enables this for the asset root in debug
         * builds; a second call replaces the watched root.
         */
        void enable_hot_reload(const std::filesystem::path& root);

        /** @brief Stops watching for changes (a no-op when not watching). */
        void disable_hot_reload();

        /** @brief Whether @ref enable_hot_reload is in effect. */
        bool hot_reload_enabled() const noexcept;

        /** @brief Minimum milliseconds between two directory scans of the hot-reload watcher. */
        static constexpr uint64_t k_hot_reload_interval_ms = 1000;

        /**
         * @brief Installs the material factory @ref load_gltf_async builds
         *        materials with when the caller passes none. The engine
         *        installs a @c gltf_standard_material_factory at start-up;
         *        @ref quit drops it.
         */
        void set_gltf_material_factory(std::shared_ptr<gltf_material_factory> factory);

        /**
         * @brief Starts loading the .gltf / .glb at @p path in the
         *        background and returns its handle at once.
         *
         * The worker pool runs @c begin_gltf_import (parse and validate
         * through the VFS, decode every texture image, build every
         * primitive's geometry, the nodes, skeleton and clips); once it has
         * finished, @ref pump runs @c finish_gltf_import on the main thread
         * (the geometry and texture uploads through this cache, the
         * materials through @p factory, or the installed one when null) and
         * marks the handle @c ready, or @c failed with the reason logged. It
         * never throws. The model shares its meshes and textures with the
         * cache exactly as one from @c load_gltf does, and each call returns
         * a model of its own.
         */
        std::shared_ptr<gltf_asset> load_gltf_async(const std::filesystem::path& path);
        std::shared_ptr<gltf_asset> load_gltf_async(const std::filesystem::path& path,
                                                    const gltf_import_options& options,
                                                    std::shared_ptr<gltf_material_factory> factory = nullptr);

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
                                                               const image& image,
                                                               gpu::color_space space = gpu::color_space::srgb);

        /**
         * @brief Returns the texture for the file @p path, taking the
         *        already-decoded @p image on a miss instead of reading the
         *        file again.
         *
         * Keyed exactly like @ref load_texture (the file's canonical
         * identity plus @p space), so the result is the very asset
         * @ref load_texture returns for that file and hot reload follows it;
         * @p image is read only on a miss and never retained. For decodes
         * made off the main thread — the asynchronous glTF import decodes
         * the model's image files on a worker.
         */
        std::shared_ptr<texture_asset> adopt_texture_image(const std::filesystem::path& path,
                                                           const image& image,
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
            std::size_t pending_loads{0};   /**< Asynchronous loads (and reloads) not yet resolved by @ref pump. */
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
        // One piece of asynchronous work. `run` executes on a worker (or
        // inline without a pool) and touches only the state its closure
        // owns, never the device or the indices; `done` is then set under
        // `m_pending_mutex`. pump() runs `complete` on the main thread for
        // each finished job, and quit() runs `abandon` instead for every
        // job still queued.
        struct pending_job
        {
            std::function<void()> run;
            std::function<void()> complete;
            std::function<void()> abandon;
            bool done{false};
        };

        // Queues @p job and hands its `run` to the worker pool.
        void submit(const std::shared_ptr<pending_job>& job);

        // Runs @p job's worker half (on whichever thread) and flags it done.
        void run_pending(const std::shared_ptr<pending_job>& job);

        // Decodes @p path on the worker pool and, in pump(), uploads it into
        // @p asset: resolving an asynchronous load, or (@p reload) swapping
        // a hot-reloaded texture in.
        void queue_texture_decode(const std::shared_ptr<texture_asset>& asset,
                                  const std::filesystem::path& path,
                                  gpu::color_space space,
                                  bool reload);

        // Scans the watched directory (rate-limited) and queues a reload of
        // every live texture whose file changed.
        void poll_hot_reload();

        // The block-compressed families the live device samples, queried on
        // first use (main thread) and kept until quit().
        const compressed_format_support& compressed_support();

        // Guards the three indices.
        mutable std::shared_mutex m_mutex;
        std::unordered_map<std::string, std::weak_ptr<texture_asset>> m_textures;
        std::unordered_map<std::string, std::weak_ptr<font_asset>> m_fonts;
        std::unordered_map<std::string, std::weak_ptr<mesh_asset>> m_meshes;

        // Asynchronous loading.
        core::job_pool* m_jobs{nullptr};
        std::shared_ptr<texture_asset> m_placeholder;
        mutable std::mutex m_pending_mutex;
        std::condition_variable m_pending_changed;
        std::vector<std::shared_ptr<pending_job>> m_pending;
        std::size_t m_pump_count{0};
        std::optional<compressed_format_support> m_compressed_support;
        std::shared_ptr<gltf_material_factory> m_gltf_factory;

        // Hot reload (main thread only): the watcher, when enabled, the time
        // of its last scan, and the assets with a reload in flight.
        std::unique_ptr<core::platform::directory_watcher> m_watcher;
        uint64_t m_last_watch_ms{0};
        std::unordered_set<const texture_asset*> m_reloading;
    };
} // namespace rendering_engine
