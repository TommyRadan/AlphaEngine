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
#include <iterator>
#include <utility>

#include <core/jobs.hpp>
#include <core/log.hpp>
#include <core/platform/platform.hpp>
#include <core/vfs/vfs.hpp>
#include <rendering_engine/assets/asset_device.hpp>
#include <rendering_engine/assets/cache_key.hpp>
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

        // Upload @p image as a mipmapped, repeat-addressed 2D texture in the
        // RGBA8 format for @p space into @p asset, which then owns the
        // handle. Shared by the synchronous, in-memory and asynchronous
        // loaders so all three produce identical textures.
        void upload_texture(texture_asset& asset, const util::image& image, gpu::color_space space)
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

            asset.texture = texture;
            asset.format = descriptor.format;
            asset.width = image.get_width();
            asset.height = image.get_height();
            asset.owns_texture = true;
            asset.state = texture_asset::load_state::ready;
        }

        std::shared_ptr<texture_asset> upload_texture(const util::image& image, gpu::color_space space)
        {
            auto asset = std::make_shared<texture_asset>();
            upload_texture(*asset, image, space);
            return asset;
        }
    } // namespace

    asset_cache::asset_cache() = default;

    asset_cache::~asset_cache()
    {
        // A worker may still hold a pending record (and its asset); let it
        // finish before the indices go, as quit() would have.
        wait_pending();
    }

    void asset_cache::init()
    {
        LOG_INF("Init Asset Cache");
    }

    void asset_cache::quit()
    {
        LOG_INF("Quit Asset Cache");

        // Loads still decoding finish on their workers (they never touch the
        // device), then are dropped unresolved: their assets keep the
        // placeholder handle, which is released right after.
        wait_pending();
        std::vector<std::shared_ptr<pending_texture>> abandoned;
        {
            std::lock_guard<std::mutex> lock{m_pending_mutex};
            abandoned.swap(m_pending);
        }
        for (const auto& job : abandoned)
        {
            job->asset->state = texture_asset::load_state::failed;
        }
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

        // Miss: decode the image (throws on failure) and upload it once.
        const util::image image{core::platform::path_to_utf8(path)};
        auto asset = upload_texture(image, space);
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
            m_placeholder = upload_texture(pixel, gpu::color_space::linear);
        }
        return m_placeholder;
    }

    void asset_cache::decode_pending(const std::shared_ptr<pending_texture>& job)
    {
        util::image image;
        std::string error;
        try
        {
            image = util::image{core::platform::path_to_utf8(job->path)};
        }
        catch (const std::exception& e)
        {
            // util::image already logged the decoder's reason; keep it for the
            // main-thread report too.
            error = e.what();
        }
        catch (...)
        {
            error = "unknown decode failure";
        }

        {
            std::lock_guard<std::mutex> lock{m_pending_mutex};
            job->image = std::move(image);
            job->error = std::move(error);
            job->done = true;
        }
        m_pending_changed.notify_all();
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

        auto job = std::make_shared<pending_texture>();
        job->asset = asset;
        job->path = path;
        job->label = core::platform::path_to_utf8(path);
        job->space = space;
        {
            std::lock_guard<std::mutex> lock{m_pending_mutex};
            m_pending.push_back(job);
        }

        if (m_jobs != nullptr)
        {
            m_jobs->dispatch([this, job] { decode_pending(job); });
        }
        else
        {
            decode_pending(job);
        }
        return asset;
    }

    std::size_t asset_cache::pump()
    {
        std::vector<std::shared_ptr<pending_texture>> completed;
        {
            std::lock_guard<std::mutex> lock{m_pending_mutex};
            const auto first_done =
                std::stable_partition(m_pending.begin(),
                                      m_pending.end(),
                                      [](const std::shared_ptr<pending_texture>& job) { return !job->done; });
            completed.assign(std::make_move_iterator(first_done), std::make_move_iterator(m_pending.end()));
            m_pending.erase(first_done, m_pending.end());
        }

        for (const auto& job : completed)
        {
            if (!job->error.empty())
            {
                LOG_ERR("asset_cache: asynchronous load of '%s' failed (%s); keeping the placeholder",
                        job->label.c_str(),
                        job->error.c_str());
                job->asset->state = texture_asset::load_state::failed;
                continue;
            }
            upload_texture(*job->asset, job->image, job->space);
            LOG_DBG("asset_cache: resolved '%s' (%ux%u)", job->label.c_str(), job->asset->width, job->asset->height);
        }

        if (++m_pump_count % k_sweep_interval == 0)
        {
            collect_unused();
        }
        return completed.size();
    }

    void asset_cache::wait_pending()
    {
        std::unique_lock<std::mutex> lock{m_pending_mutex};
        m_pending_changed.wait(lock,
                               [this]
                               {
                                   return std::all_of(m_pending.begin(),
                                                      m_pending.end(),
                                                      [](const std::shared_ptr<pending_texture>& job)
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

        auto asset = upload_texture(image, space);
        std::unique_lock lock{m_mutex};
        m_textures[full_key] = asset;
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
