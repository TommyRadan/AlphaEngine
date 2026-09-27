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

#include <rendering_engine/assets/asset_cache.hpp>

#include <algorithm>
#include <exception>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <core/jobs.hpp>
#include <core/log.hpp>
#include <core/platform/directory_watcher.hpp>
#include <core/platform/platform.hpp>
#include <core/vfs/vfs.hpp>
#include <rendering_engine/assets/asset_device.hpp>
#include <rendering_engine/assets/cache_key.hpp>
#include <rendering_engine/assets/gltf_importer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/util/color.hpp>
#include <rendering_engine/util/image.hpp>

namespace rendering_engine
{
    namespace
    {
        // The stable identity of the file @p path names: resolved through the
        // VFS, made canonical and case-folded where the filesystem ignores
        // case, so "a/./b.png", "a/b.png" and a mount-relative spelling of
        // the same file collapse to one entry.
        std::string path_key(const std::filesystem::path& path)
        {
            return core::default_vfs().canonical_key(path);
        }

        // The colour-space suffix of a texture key. An sRGB and a linear
        // upload of one file are different GPU resources (the sampler
        // decodes one and not the other), so they must not alias.
        const char* color_space_key(gpu::color_space space)
        {
            return space == gpu::color_space::srgb ? "srgb" : "linear";
        }

        std::string texture_key(const std::string& identity, gpu::color_space space)
        {
            return identity + '|' + color_space_key(space);
        }

        // Erase every entry in @p map whose asset has been freed, returning the
        // number removed. Shared by all three asset maps. Caller holds the
        // exclusive lock.
        template<typename Map>
        std::size_t sweep_expired(Map& map)
        {
            std::size_t removed = 0;
            for (auto it = map.begin(); it != map.end();)
            {
                if (it->second.expired())
                {
                    it = map.erase(it);
                    ++removed;
                }
                else
                {
                    ++it;
                }
            }
            return removed;
        }

        // Count entries in @p map whose asset is still referenced. Caller
        // holds at least the shared lock.
        template<typename Map>
        std::size_t count_live(const Map& map)
        {
            std::size_t live = 0;
            for (const auto& entry : map)
            {
                if (!entry.second.expired())
                {
                    ++live;
                }
            }
            return live;
        }

        // The live asset under @p key in @p map, or null. Caller holds at
        // least the shared lock.
        template<typename T>
        std::shared_ptr<T> find_live(const std::unordered_map<std::string, std::weak_ptr<T>>& map,
                                     const std::string& key)
        {
            if (auto it = map.find(key); it != map.end())
            {
                return it->second.lock();
            }
            return nullptr;
        }

        // What a texture upload produced, before it is installed in an asset.
        // An invalid texture means the device refused it (logged).
        struct uploaded_texture
        {
            gpu::texture texture{};
            gpu::texture_format format{gpu::texture_format::rgba8_unorm};
            uint32_t width{0};
            uint32_t height{0};
        };

        // Upload @p image as a mipmapped, repeat-addressed 2D texture in the
        // RGBA8 format for @p space. Shared by every loader so an image
        // file, an in-memory image and an asynchronous load all produce
        // identical textures.
        uploaded_texture upload_image(const util::image& image, gpu::color_space space)
        {
            auto& gpu = asset_device();
            gpu::texture_descriptor descriptor{};
            descriptor.dimension = gpu::texture_dimension::d2;
            descriptor.format = gpu::rgba8_format(space);
            descriptor.width = image.get_width();
            descriptor.height = image.get_height();
            descriptor.mipmaps = true;
            descriptor.min_filter = gpu::filter_mode::linear;
            descriptor.mag_filter = gpu::filter_mode::linear;
            descriptor.mipmap_filter = gpu::mipmap_mode::linear;
            descriptor.address_u = gpu::address_mode::repeat;
            descriptor.address_v = gpu::address_mode::repeat;
            descriptor.address_w = gpu::address_mode::repeat;

            const gpu::texture texture = gpu.create_texture(descriptor);
            const size_t pixel_bytes =
                static_cast<size_t>(image.get_width()) * static_cast<size_t>(image.get_height()) * sizeof(util::color);
            gpu.write_texture(texture, image.get_pixels(), pixel_bytes);
            gpu.generate_mipmaps(texture);
            return uploaded_texture{texture, descriptor.format, image.get_width(), image.get_height()};
        }

        // Upload @p decoded: its image as upload_image does, or its
        // pre-built levels (a KTX2 file) as they are, with the same sampler
        // state. Block-compressed textures are created sampled-only: they
        // can be neither attached nor copied from.
        uploaded_texture
        upload_decoded(const decoded_texture& decoded, gpu::color_space space, const std::string& label)
        {
            if (!decoded.has_levels())
            {
                return upload_image(decoded.image, space);
            }

            auto& gpu = asset_device();
            gpu::texture_descriptor descriptor{};
            descriptor.dimension = gpu::texture_dimension::d2;
            descriptor.format = decoded.format;
            descriptor.width = decoded.width;
            descriptor.height = decoded.height;
            descriptor.mip_level_count = static_cast<uint32_t>(decoded.levels.size());
            descriptor.min_filter = gpu::filter_mode::linear;
            descriptor.mag_filter = gpu::filter_mode::linear;
            descriptor.mipmap_filter = gpu::mipmap_mode::linear;
            descriptor.address_u = gpu::address_mode::repeat;
            descriptor.address_v = gpu::address_mode::repeat;
            descriptor.address_w = gpu::address_mode::repeat;
            if (gpu::is_compressed_texture_format(decoded.format))
            {
                descriptor.usage = gpu::texture_usage_sampled | gpu::texture_usage_copy_dst;
            }

            const gpu::texture texture = gpu.create_texture(descriptor);
            if (!texture.valid())
            {
                LOG_ERR("asset_cache: the device could not create a %ux%u texture for '%s'",
                        decoded.width,
                        decoded.height,
                        label.c_str());
                return {};
            }
            for (uint32_t level = 0; level < descriptor.mip_level_count; ++level)
            {
                gpu::texture_write_region region{};
                region.mip_level = level;
                region.width = std::max(1u, decoded.width >> level);
                region.height = std::max(1u, decoded.height >> level);
                const std::vector<std::byte>& bytes = decoded.levels[level];
                if (!gpu.write_texture_region(texture, region, bytes.data(), bytes.size()))
                {
                    LOG_ERR("asset_cache: level %u of '%s' could not be uploaded", level, label.c_str());
                    gpu.destroy(texture);
                    return {};
                }
            }
            return uploaded_texture{texture, decoded.format, decoded.width, decoded.height};
        }

        // Makes @p uploaded the texture of @p asset, which owns it from now on.
        void install(texture_asset& asset, const uploaded_texture& uploaded)
        {
            asset.texture = uploaded.texture;
            asset.format = uploaded.format;
            asset.width = uploaded.width;
            asset.height = uploaded.height;
            asset.owns_texture = true;
            asset.state = texture_asset::load_state::ready;
        }

        // Replaces the texture of an @p asset already handed out with
        // @p uploaded: releases the previous one unless it is the shared
        // placeholder, and moves the generation so the consumers that bound
        // the old handle rebuild.
        void replace(texture_asset& asset, const uploaded_texture& uploaded)
        {
            const gpu::texture previous = asset.texture;
            const bool owned_previous = asset.owns_texture;
            install(asset, uploaded);
            ++asset.generation;
            if (owned_previous && previous.valid())
            {
                asset_device().destroy(previous);
            }
        }

        std::shared_ptr<texture_asset> make_asset(const uploaded_texture& uploaded)
        {
            auto asset = std::make_shared<texture_asset>();
            install(*asset, uploaded);
            return asset;
        }

        const char* yes_no(bool value)
        {
            return value ? "yes" : "no";
        }
    } // namespace

    asset_cache::asset_cache() = default;

    asset_cache::~asset_cache()
    {
        // A worker may still be running a job (which reaches back into this
        // cache to flag it done); let it finish before the members go, as
        // quit() would have.
        wait_pending();
    }

    void asset_cache::init()
    {
        LOG_INF("Init Asset Cache");
    }

    void asset_cache::quit()
    {
        LOG_INF("Quit Asset Cache");

        disable_hot_reload();

        // Jobs still running finish on their workers (they never touch the
        // device), then are dropped unresolved: a loading texture keeps the
        // placeholder handle, which is released right after, and a glTF
        // load is marked failed.
        wait_pending();
        std::vector<std::shared_ptr<pending_job>> abandoned;
        {
            std::lock_guard<std::mutex> lock{m_pending_mutex};
            abandoned.swap(m_pending);
        }
        for (const auto& job : abandoned)
        {
            job->abandon();
        }
        m_reloading.clear();
        m_gltf_factory.reset();
        m_compressed_support.reset();
        m_placeholder.reset();

        std::unique_lock lock{m_mutex};
        m_textures.clear();
        m_fonts.clear();
        m_meshes.clear();
    }

    void asset_cache::set_jobs(core::jobs* jobs)
    {
        m_jobs = jobs;
    }

    const compressed_format_support& asset_cache::compressed_support()
    {
        if (!m_compressed_support.has_value())
        {
            m_compressed_support = compressed_format_support::query(asset_device());
            LOG_INF("asset_cache: block-compressed formats the device samples: BC1 %s, BC3 %s, BC4 %s, BC5 %s, "
                    "BC7 %s, ASTC 4x4 %s",
                    yes_no(m_compressed_support->bc1),
                    yes_no(m_compressed_support->bc3),
                    yes_no(m_compressed_support->bc4),
                    yes_no(m_compressed_support->bc5),
                    yes_no(m_compressed_support->bc7),
                    yes_no(m_compressed_support->astc_4x4));
        }
        return *m_compressed_support;
    }

    std::shared_ptr<texture_asset> asset_cache::load_texture(const std::filesystem::path& path, gpu::color_space space)
    {
        const std::string key = texture_key(path_key(path), space);
        {
            std::shared_lock lock{m_mutex};
            if (auto existing = find_live(m_textures, key))
            {
                return existing;
            }
        }

        // Miss: decode the file (throws on failure) and upload it once. Only
        // a KTX2 file needs to know what the device samples.
        const decoded_texture decoded =
            decode_texture_file(path, space, is_ktx2_path(path) ? compressed_support() : compressed_format_support{});
        const std::string label = core::platform::path_to_utf8(path);
        const uploaded_texture uploaded = upload_decoded(decoded, space, label);
        if (!uploaded.texture.valid())
        {
            throw std::runtime_error{"Could not upload texture (" + label + ")"};
        }
        auto asset = make_asset(uploaded);
        std::unique_lock lock{m_mutex};
        m_textures[key] = asset;
        return asset;
    }

    std::shared_ptr<texture_asset> asset_cache::placeholder_texture()
    {
        if (m_placeholder == nullptr)
        {
            // 1x1 mid grey: samples as 0.5 in every channel whatever the
            // colour space of the asset standing in on it, which keeps a
            // still-loading albedo or data map from flashing black or white.
            const util::image pixel{1, 1, util::color{128, 128, 128, 255}};
            m_placeholder = make_asset(upload_image(pixel, gpu::color_space::linear));
        }
        return m_placeholder;
    }

    void asset_cache::submit(const std::shared_ptr<pending_job>& job)
    {
        {
            std::lock_guard<std::mutex> lock{m_pending_mutex};
            m_pending.push_back(job);
        }
        if (m_jobs != nullptr)
        {
            m_jobs->dispatch([this, job] { run_pending(job); });
        }
        else
        {
            run_pending(job);
        }
    }

    void asset_cache::run_pending(const std::shared_ptr<pending_job>& job)
    {
        // The closure keeps its own results and catches its own failures.
        job->run();
        // Notify while still holding the lock: a waiter that wakes between
        // an unlocked "done = true" and this notify could see every job
        // done and return, letting the destructor tear the mutex and
        // condition variable down while this thread still touches them.
        std::lock_guard<std::mutex> lock{m_pending_mutex};
        job->done = true;
        m_pending_changed.notify_all();
    }

    void asset_cache::queue_texture_decode(const std::shared_ptr<texture_asset>& asset,
                                           const std::filesystem::path& path,
                                           gpu::color_space space,
                                           bool reload)
    {
        // Everything the worker reads and writes; nothing else is shared
        // with it.
        struct decode_state
        {
            std::filesystem::path path;
            std::string label; // the path as given, for the log
            gpu::color_space space{gpu::color_space::srgb};
            compressed_format_support support;
            decoded_texture decoded;
            std::string error;
        };
        auto state = std::make_shared<decode_state>();
        state->path = path;
        state->label = core::platform::path_to_utf8(path);
        state->space = space;
        if (is_ktx2_path(path))
        {
            // Queried here, on the main thread: the worker must not touch
            // the device.
            state->support = compressed_support();
        }

        auto job = std::make_shared<pending_job>();
        job->run = [state]
        {
            try
            {
                state->decoded = decode_texture_file(state->path, state->space, state->support);
            }
            catch (const std::exception& e)
            {
                // The decoder already logged its reason; keep it for the
                // main-thread report too.
                state->error = e.what();
            }
            catch (...)
            {
                state->error = "unknown decode failure";
            }
        };

        if (reload)
        {
            // A reload holds the asset weakly: if every holder lets go while
            // the file decodes, there is nothing left to swap into.
            const texture_asset* key = asset.get();
            const std::weak_ptr<texture_asset> target = asset;
            m_reloading.insert(key);
            job->complete = [this, state, target, key]
            {
                m_reloading.erase(key);
                const std::shared_ptr<texture_asset> live = target.lock();
                if (live == nullptr)
                {
                    return;
                }
                if (!state->error.empty())
                {
                    LOG_WRN("asset_cache: hot reload of '%s' failed (%s); keeping the previous texture",
                            state->label.c_str(),
                            state->error.c_str());
                    return;
                }
                const uploaded_texture uploaded = upload_decoded(state->decoded, state->space, state->label);
                if (!uploaded.texture.valid())
                {
                    return;
                }
                replace(*live, uploaded);
                LOG_INF("asset_cache: hot-reloaded '%s' (%ux%u)", state->label.c_str(), live->width, live->height);
            };
            job->abandon = [this, key] { m_reloading.erase(key); };
        }
        else
        {
            job->complete = [state, asset]
            {
                if (!state->error.empty())
                {
                    LOG_ERR("asset_cache: asynchronous load of '%s' failed (%s); keeping the placeholder",
                            state->label.c_str(),
                            state->error.c_str());
                    asset->state = texture_asset::load_state::failed;
                    return;
                }
                const uploaded_texture uploaded = upload_decoded(state->decoded, state->space, state->label);
                if (!uploaded.texture.valid())
                {
                    asset->state = texture_asset::load_state::failed;
                    return;
                }
                replace(*asset, uploaded);
                LOG_DBG("asset_cache: resolved '%s' (%ux%u)", state->label.c_str(), asset->width, asset->height);
            };
            job->abandon = [asset] { asset->state = texture_asset::load_state::failed; };
        }
        submit(job);
    }

    std::shared_ptr<texture_asset> asset_cache::load_texture_async(const std::filesystem::path& path,
                                                                   gpu::color_space space)
    {
        const std::string key = texture_key(path_key(path), space);
        {
            std::shared_lock lock{m_mutex};
            if (auto existing = find_live(m_textures, key))
            {
                return existing;
            }
        }

        // Miss: hand out an asset that stands in on the placeholder and queue
        // the decode. The upload happens in pump(), on the main thread.
        const std::shared_ptr<texture_asset> placeholder = placeholder_texture();
        auto asset = std::make_shared<texture_asset>();
        asset->texture = placeholder->texture;
        asset->format = placeholder->format;
        asset->width = placeholder->width;
        asset->height = placeholder->height;
        asset->owns_texture = false;
        asset->state = texture_asset::load_state::loading;
        {
            std::unique_lock lock{m_mutex};
            // Re-check under the exclusive lock: another caller may have
            // queued the same key between the shared-lock check above and
            // here, in which case join its load instead of queueing a
            // second decode and clobbering its index entry.
            if (auto existing = find_live(m_textures, key))
            {
                return existing;
            }
            m_textures[key] = asset;
        }

        queue_texture_decode(asset, path, space, false);
        return asset;
    }

    std::size_t asset_cache::pump()
    {
        std::vector<std::shared_ptr<pending_job>> completed;
        {
            std::lock_guard<std::mutex> lock{m_pending_mutex};
            const auto first_done = std::stable_partition(
                m_pending.begin(), m_pending.end(), [](const std::shared_ptr<pending_job>& job) { return !job->done; });
            completed.assign(std::make_move_iterator(first_done), std::make_move_iterator(m_pending.end()));
            m_pending.erase(first_done, m_pending.end());
        }

        for (const auto& job : completed)
        {
            job->complete();
        }

        if (m_watcher != nullptr)
        {
            poll_hot_reload();
        }

        if (++m_pump_count % k_sweep_interval == 0)
        {
            collect_unused();
        }
        return completed.size();
    }

    void asset_cache::enable_hot_reload(const std::filesystem::path& root)
    {
        // Absolute, so every change the watcher reports is a native path
        // whose canonical identity matches the cache key of the same file
        // loaded through the VFS.
        std::error_code error;
        std::filesystem::path watched = std::filesystem::absolute(root, error);
        if (error)
        {
            watched = root;
        }
        m_watcher = std::make_unique<core::platform::directory_watcher>(watched);
        m_last_watch_ms = core::platform::ticks_ms();
        LOG_INF("asset_cache: hot reload watching %s (%zu files)",
                core::platform::path_to_utf8(watched).c_str(),
                m_watcher->tracked_count());
    }

    void asset_cache::disable_hot_reload()
    {
        m_watcher.reset();
    }

    bool asset_cache::hot_reload_enabled() const noexcept
    {
        return m_watcher != nullptr;
    }

    void asset_cache::poll_hot_reload()
    {
        const uint64_t now = core::platform::ticks_ms();
        if (now - m_last_watch_ms < k_hot_reload_interval_ms)
        {
            return;
        }
        m_last_watch_ms = now;

        for (const core::platform::file_change& change : m_watcher->poll())
        {
            // A deleted file leaves its texture as it was; an editor that
            // saves by replacing the file reports it modified (or added).
            if (change.change == core::platform::file_change::kind::removed)
            {
                continue;
            }
            const std::string identity = path_key(change.path);
            for (const gpu::color_space space : {gpu::color_space::srgb, gpu::color_space::linear})
            {
                std::shared_ptr<texture_asset> asset;
                {
                    std::shared_lock lock{m_mutex};
                    asset = find_live(m_textures, texture_key(identity, space));
                }
                if (asset == nullptr || asset->state == texture_asset::load_state::loading ||
                    m_reloading.count(asset.get()) != 0)
                {
                    continue;
                }
                queue_texture_decode(asset, change.path, space, true);
            }
        }
    }

    void asset_cache::set_gltf_material_factory(std::shared_ptr<gltf_material_factory> factory)
    {
        m_gltf_factory = std::move(factory);
    }

    std::shared_ptr<gltf_asset> asset_cache::load_gltf_async(const std::filesystem::path& path)
    {
        return load_gltf_async(path, gltf_import_options{}, nullptr);
    }

    std::shared_ptr<gltf_asset> asset_cache::load_gltf_async(const std::filesystem::path& path,
                                                             const gltf_import_options& options,
                                                             std::shared_ptr<gltf_material_factory> factory)
    {
        auto asset = std::make_shared<gltf_asset>();
        const std::string label = core::platform::path_to_utf8(path);
        if (factory == nullptr)
        {
            factory = m_gltf_factory;
        }
        if (factory == nullptr)
        {
            asset->state = gltf_asset::load_state::failed;
            asset->error = "no glTF material factory is installed";
            LOG_ERR("asset_cache: cannot load glTF '%s': %s", label.c_str(), asset->error.c_str());
            return asset;
        }

        // Everything the worker reads and writes; nothing else is shared
        // with it.
        struct import_state
        {
            std::filesystem::path path;
            gltf_import_options options;
            gltf_import_ptr import;
            std::string error;
        };
        auto state = std::make_shared<import_state>();
        state->path = path;
        state->options = options;

        auto job = std::make_shared<pending_job>();
        job->run = [state]
        {
            try
            {
                state->import = begin_gltf_import(state->path, state->options, true);
            }
            catch (const std::exception& e)
            {
                state->error = e.what(); // the importer logged why
            }
            catch (...)
            {
                state->error = "unknown import failure";
            }
        };
        job->complete = [this, state, asset, factory, label]
        {
            if (state->import != nullptr)
            {
                try
                {
                    asset->model = finish_gltf_import(*state->import, *this, *factory);
                    asset->state = gltf_asset::load_state::ready;
                }
                catch (const std::exception& e)
                {
                    state->error = e.what();
                }
                // The parsed file, its decoded images and any geometry the
                // cache already held go now rather than with the handle.
                state->import.reset();
            }
            if (asset->state != gltf_asset::load_state::ready)
            {
                asset->state = gltf_asset::load_state::failed;
                asset->error = state->error.empty() ? "unknown import failure" : state->error;
                LOG_ERR("asset_cache: asynchronous load of glTF '%s' failed (%s)", label.c_str(), asset->error.c_str());
            }
        };
        job->abandon = [asset]
        {
            asset->state = gltf_asset::load_state::failed;
            asset->error = "the asset cache shut down before the load finished";
        };
        submit(job);
        return asset;
    }

    void asset_cache::wait_pending()
    {
        std::unique_lock<std::mutex> lock{m_pending_mutex};
        m_pending_changed.wait(lock,
                               [this]
                               {
                                   return std::all_of(m_pending.begin(),
                                                      m_pending.end(),
                                                      [](const std::shared_ptr<pending_job>& job)
                                                      { return job->done; });
                               });
    }

    std::size_t asset_cache::pending_count() const
    {
        std::lock_guard<std::mutex> lock{m_pending_mutex};
        return m_pending.size();
    }

    std::shared_ptr<texture_asset>
    asset_cache::load_texture_from_image(const std::string& key, const util::image& image, gpu::color_space space)
    {
        const std::string full_key = texture_key(key, space);
        {
            std::shared_lock lock{m_mutex};
            if (auto existing = find_live(m_textures, full_key))
            {
                return existing;
            }
        }

        auto asset = make_asset(upload_image(image, space));
        std::unique_lock lock{m_mutex};
        m_textures[full_key] = asset;
        return asset;
    }

    std::shared_ptr<texture_asset> asset_cache::adopt_texture_image(const std::filesystem::path& path,
                                                                    const util::image& image,
                                                                    gpu::color_space space)
    {
        const std::string key = texture_key(path_key(path), space);
        {
            std::shared_lock lock{m_mutex};
            if (auto existing = find_live(m_textures, key))
            {
                return existing;
            }
        }

        auto asset = make_asset(upload_image(image, space));
        std::unique_lock lock{m_mutex};
        m_textures[key] = asset;
        return asset;
    }

    std::shared_ptr<font_asset> asset_cache::load_font(const std::filesystem::path& path, float size)
    {
        // Glyph rasterization is size-specific, so the size is part of the key.
        const std::string key = path_key(path) + '|' + cache_key_number(size);
        {
            std::shared_lock lock{m_mutex};
            if (auto existing = find_live(m_fonts, key))
            {
                return existing;
            }
        }

        auto asset = std::make_shared<font_asset>(core::platform::path_to_utf8(path), size);
        std::unique_lock lock{m_mutex};
        m_fonts[key] = asset;
        return asset;
    }

    std::shared_ptr<mesh_asset> asset_cache::get_or_create_mesh(const std::string& key,
                                                                const std::function<mesh_data()>& builder)
    {
        {
            std::shared_lock lock{m_mutex};
            if (auto existing = find_live(m_meshes, key))
            {
                return existing;
            }
        }

        // Miss: build the geometry (outside the lock — a builder may reach
        // back into the cache) and upload it once.
        const mesh_data data = builder();
        if (data.vertex_stride == 0 || data.vertex_bytes.empty())
        {
            LOG_ERR("asset_cache: mesh builder for '%s' produced no geometry (stride %u, %zu bytes); nothing uploaded",
                    key.c_str(),
                    data.vertex_stride,
                    data.vertex_bytes.size());
            return nullptr;
        }

        // A named format promises the matching struct's stride; a builder
        // that claims one over differently sized records would let a
        // renderable pass the format check and still fetch past the vertex,
        // so demote it to custom (stride-checked only) rather than trust it.
        vertex_format format = data.format;
        if (format != vertex_format::custom && vertex_format_stride(format) != data.vertex_stride)
        {
            LOG_WRN("asset_cache: mesh '%s' claims format %s (stride %u) but has stride %u; treating as custom",
                    key.c_str(),
                    vertex_format_name(format),
                    vertex_format_stride(format),
                    data.vertex_stride);
            format = vertex_format::custom;
        }

        auto& gpu = asset_device();
        auto asset = std::make_shared<mesh_asset>();
        asset->key = key;

        gpu::buffer_descriptor vertex_descriptor{};
        vertex_descriptor.size = data.vertex_bytes.size();
        vertex_descriptor.usage = gpu::buffer_usage_vertex;
        vertex_descriptor.initial_data = data.vertex_bytes.data();
        asset->vertex_buffer = gpu.create_buffer(vertex_descriptor);
        asset->vertex_stride = data.vertex_stride;
        asset->format = format;
        asset->vertex_count = static_cast<uint32_t>(data.vertex_bytes.size() / data.vertex_stride);

        // Object-space bounds for frustum culling: trust the builder's box
        // when it supplied one (an importer's record may not lead with the
        // position), otherwise derive it from the positions once here so
        // every renderable sharing this upload shares the box too.
        if (data.bounds.has_value())
        {
            asset->bounds = *data.bounds;
        }
        else if (const auto computed = data.compute_bounds(); computed.has_value())
        {
            asset->bounds = *computed;
        }

        if (!data.indices.empty())
        {
            gpu::buffer_descriptor index_descriptor{};
            index_descriptor.size = data.indices.size() * sizeof(uint32_t);
            index_descriptor.usage = gpu::buffer_usage_index;
            index_descriptor.initial_data = data.indices.data();
            asset->index_buffer = gpu.create_buffer(index_descriptor);
            asset->index_count = static_cast<uint32_t>(data.indices.size());
        }

        std::unique_lock lock{m_mutex};
        if (auto raced = find_live(m_meshes, key))
        {
            // The builder (or something it called) cached this key meanwhile;
            // share that upload and let ours go with the local handle.
            return raced;
        }
        m_meshes[key] = asset;
        return asset;
    }

    std::shared_ptr<mesh_asset> asset_cache::find_mesh(const std::string& key) const
    {
        std::shared_lock lock{m_mutex};
        return find_live(m_meshes, key);
    }

    std::size_t asset_cache::collect_unused()
    {
        std::unique_lock lock{m_mutex};
        return sweep_expired(m_textures) + sweep_expired(m_fonts) + sweep_expired(m_meshes);
    }

    asset_cache::stats asset_cache::statistics() const
    {
        stats result;
        {
            std::shared_lock lock{m_mutex};
            result.textures = count_live(m_textures);
            result.texture_entries = m_textures.size();
            result.fonts = count_live(m_fonts);
            result.font_entries = m_fonts.size();
            result.meshes = count_live(m_meshes);
            result.mesh_entries = m_meshes.size();
        }
        result.pending_loads = pending_count();
        return result;
    }

    std::size_t asset_cache::texture_count() const
    {
        std::shared_lock lock{m_mutex};
        return count_live(m_textures);
    }

    std::size_t asset_cache::font_count() const
    {
        std::shared_lock lock{m_mutex};
        return count_live(m_fonts);
    }

    std::size_t asset_cache::mesh_count() const
    {
        std::shared_lock lock{m_mutex};
        return count_live(m_meshes);
    }
} // namespace rendering_engine
