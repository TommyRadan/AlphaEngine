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

#include <core/log.hpp>
#include <rendering_engine/assets/asset_device.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/util/color.hpp>
#include <rendering_engine/util/image.hpp>

namespace rendering_engine
{
    namespace
    {
        // Collapse a path to a stable cache key so "a/./b.png" and "a/b.png"
        // resolve to one entry. generic_string() normalizes the separator so
        // the key is identical across platforms.
        std::string path_key(const std::filesystem::path& path)
        {
            return path.lexically_normal().generic_string();
        }

        // The colour-space suffix of a texture key. An sRGB and a linear
        // upload of one file are different GPU resources (the sampler
        // decodes one and not the other), so they must not alias.
        const char* color_space_key(gpu::color_space space)
        {
            return space == gpu::color_space::srgb ? "srgb" : "linear";
        }

        // Erase every entry in @p map whose asset has been freed, returning the
        // number removed. Shared by all three asset maps.
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

        // Count entries in @p map whose asset is still referenced.
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
    } // namespace

    asset_cache::asset_cache() = default;

    void asset_cache::init()
    {
        LOG_INF("Init Asset Cache");
    }

    void asset_cache::quit()
    {
        LOG_INF("Quit Asset Cache");
        m_textures.clear();
        m_fonts.clear();
        m_meshes.clear();
    }

    std::shared_ptr<texture_asset> asset_cache::load_texture(const std::filesystem::path& path, gpu::color_space space)
    {
        const std::string key = path_key(path) + '|' + color_space_key(space);
        if (auto it = m_textures.find(key); it != m_textures.end())
        {
            if (auto existing = it->second.lock())
            {
                return existing;
            }
        }

        // Miss: decode the image (throws on failure) and upload it once.
        const util::image image{path.string()};

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

        auto asset = std::make_shared<texture_asset>();
        asset->format = descriptor.format;
        asset->width = image.get_width();
        asset->height = image.get_height();
        asset->texture = gpu.create_texture(descriptor);

        const size_t pixel_bytes =
            static_cast<size_t>(image.get_width()) * static_cast<size_t>(image.get_height()) * sizeof(util::color);
        gpu.write_texture(asset->texture, image.get_pixels(), pixel_bytes);
        gpu.generate_mipmaps(asset->texture);

        m_textures[key] = asset;
        return asset;
    }

    std::shared_ptr<font_asset> asset_cache::load_font(const std::filesystem::path& path, float size)
    {
        // Glyph rasterization is size-specific, so the size is part of the key.
        const std::string key = path_key(path) + '|' + std::to_string(size);
        if (auto it = m_fonts.find(key); it != m_fonts.end())
        {
            if (auto existing = it->second.lock())
            {
                return existing;
            }
        }

        auto asset = std::make_shared<font_asset>(path.string(), size);
        m_fonts[key] = asset;
        return asset;
    }

    std::shared_ptr<mesh_asset> asset_cache::get_or_create_mesh(const std::string& key,
                                                                const std::function<mesh_data()>& builder)
    {
        if (auto it = m_meshes.find(key); it != m_meshes.end())
        {
            if (auto existing = it->second.lock())
            {
                return existing;
            }
        }

        // Miss: build the geometry and upload it once.
        const mesh_data data = builder();
        if (data.vertex_stride == 0 || data.vertex_bytes.empty())
        {
            LOG_WRN("asset_cache: mesh builder for '%s' produced empty geometry", key.c_str());
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

        gpu::buffer_descriptor vertex_descriptor{};
        vertex_descriptor.size = data.vertex_bytes.size();
        vertex_descriptor.usage = gpu::buffer_usage_vertex;
        vertex_descriptor.initial_data = data.vertex_bytes.data();
        asset->vertex_buffer = gpu.create_buffer(vertex_descriptor);
        asset->vertex_stride = data.vertex_stride;
        asset->format = format;
        asset->vertex_count =
            data.vertex_stride != 0 ? static_cast<uint32_t>(data.vertex_bytes.size() / data.vertex_stride) : 0;

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

        m_meshes[key] = asset;
        return asset;
    }

    std::size_t asset_cache::collect_unused()
    {
        return sweep_expired(m_textures) + sweep_expired(m_fonts) + sweep_expired(m_meshes);
    }

    std::size_t asset_cache::texture_count() const
    {
        return count_live(m_textures);
    }

    std::size_t asset_cache::font_count() const
    {
        return count_live(m_fonts);
    }

    std::size_t asset_cache::mesh_count() const
    {
        return count_live(m_meshes);
    }
} // namespace rendering_engine
