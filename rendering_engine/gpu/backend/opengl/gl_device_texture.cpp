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
 * @file gl_device_texture.cpp
 * @brief @c gl_device member functions that manage GL textures and
 *        sampler objects: @c create_texture, @c create_sampler,
 *        @c destroy(texture), @c destroy(sampler), @c write_texture,
 *        @c write_texture_region, @c write_texture_3d,
 *        @c write_cube_face, @c generate_mipmaps.
 *
 * Textures are created with immutable storage (@c glTextureStorage2D /
 * @c 3D): the whole mip chain — and all six faces of a cube — is
 * allocated once at create time, so every level is addressable by
 * @c generate_mipmaps and the storage-image bindings and the driver
 * never re-validates completeness per draw. Uploads and parameters go
 * through the named entry points, so nothing here binds a texture or
 * touches the active texture unit a pass may be using.
 */

#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>

#include <algorithm>

#include <glad/gl.h>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_check.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_translate.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    namespace
    {
        // Level count of a full chain from the base extent down to 1.
        uint32_t full_mip_chain(uint32_t width, uint32_t height, uint32_t depth)
        {
            uint32_t levels = 1;
            uint32_t extent = std::max(width, std::max(height, depth));
            while (extent > 1)
            {
                extent >>= 1;
                ++levels;
            }
            return levels;
        }

        uint32_t level_extent(uint32_t base, uint32_t level)
        {
            return std::max(1u, base >> level);
        }

        // Bake the descriptor's sampler state onto the texture object.
        // The chain filter goes through effective_mipmap_filter, so a
        // texture that allocated a chain samples it and a single-level
        // one never asks for a mip filter.
        void apply_texture_sampler_state(GLuint texture_id, const texture_descriptor& descriptor, bool mipmapped)
        {
            const mipmap_mode mip = effective_mipmap_filter(mipmapped, descriptor.mipmap_filter);
            glTextureParameteri(
                texture_id, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(to_gl_min_filter(descriptor.min_filter, mip)));
            glTextureParameteri(
                texture_id, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(to_gl_mag_filter(descriptor.mag_filter)));
            glTextureParameteri(
                texture_id, GL_TEXTURE_WRAP_S, static_cast<GLint>(to_gl_address_mode(descriptor.address_u)));
            glTextureParameteri(
                texture_id, GL_TEXTURE_WRAP_T, static_cast<GLint>(to_gl_address_mode(descriptor.address_v)));
            glTextureParameteri(
                texture_id, GL_TEXTURE_WRAP_R, static_cast<GLint>(to_gl_address_mode(descriptor.address_w)));
        }

        // True when @p size bytes cover a tightly packed upload of
        // @p texels in @p format; logs and returns false otherwise so
        // the caller never hands GL a buffer it would read past.
        bool upload_fits(const char* where, size_t size, uint64_t texels, texture_format format)
        {
            const uint64_t required = texels * to_gl_texel_bytes(format);
            if (static_cast<uint64_t>(size) < required)
            {
                LOG_WRN("%s: %zu bytes supplied, %llu needed", where, size, static_cast<unsigned long long>(required));
                return false;
            }
            return true;
        }
    } // namespace

    texture gl_device::create_texture(const texture_descriptor& descriptor)
    {
        const uint32_t depth = descriptor.dimension == texture_dimension::d3 ? descriptor.depth : 1u;
        if (descriptor.width == 0 || descriptor.height == 0 || depth == 0)
        {
            LOG_ERR("create_texture: zero-sized texture (%ux%ux%u)", descriptor.width, descriptor.height, depth);
            return {};
        }

        gl_texture record{};
        record.target = to_gl_texture_target(descriptor.dimension);
        record.format = descriptor.format;
        record.width = descriptor.width;
        record.height = descriptor.height;
        record.depth = depth;
        record.mip_levels = descriptor.mipmaps ? full_mip_chain(record.width, record.height, record.depth) : 1u;
        record.mipmaps = record.mip_levels > 1;

        glCreateTextures(record.target, 1, &record.object_id);
        apply_texture_sampler_state(record.object_id, descriptor, record.mipmaps);

        // Immutable storage for every level. The caller fills level 0
        // via @ref write_texture, @ref write_texture_3d or
        // @ref write_cube_face and derives the rest with
        // @ref generate_mipmaps (or writes them through storage-image
        // bindings, as the IBL prefilter does).
        const auto fmt = to_gl_texture_format(descriptor.format);
        const auto levels = static_cast<GLsizei>(record.mip_levels);
        if (record.target == GL_TEXTURE_3D)
        {
            GL_CHECK(glTextureStorage3D(record.object_id,
                                        levels,
                                        fmt.internal_format,
                                        static_cast<GLsizei>(record.width),
                                        static_cast<GLsizei>(record.height),
                                        static_cast<GLsizei>(record.depth)));
        }
        else
        {
            // For a cube map this allocates all six faces of each level.
            GL_CHECK(glTextureStorage2D(record.object_id,
                                        levels,
                                        fmt.internal_format,
                                        static_cast<GLsizei>(record.width),
                                        static_cast<GLsizei>(record.height)));
        }

        texture h{};
        h.id = m_textures.insert(record);
        return h;
    }

    void gl_device::destroy(texture handle)
    {
        if (auto* record = m_textures.lookup(handle.id))
        {
            if (record->object_id != 0)
            {
                glDeleteTextures(1, &record->object_id);
            }
            m_textures.remove(handle.id);
            // The name may be recycled by the next create; a shadow
            // still holding it must not skip that texture's bind.
            invalidate_state_cache();
        }
    }

    void gl_device::write_texture(texture handle, const void* data, size_t size)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->object_id == 0 || record->target != GL_TEXTURE_2D)
        {
            LOG_WRN("write_texture: invalid 2D texture handle");
            return;
        }
        if (!upload_fits("write_texture", size, static_cast<uint64_t>(record->width) * record->height, record->format))
        {
            return;
        }
        const auto fmt = to_gl_texture_format(record->format);
        GL_CHECK(glTextureSubImage2D(record->object_id,
                                     0,
                                     0,
                                     0,
                                     static_cast<GLsizei>(record->width),
                                     static_cast<GLsizei>(record->height),
                                     fmt.upload_format,
                                     fmt.upload_type,
                                     data));
    }

    bool
    gl_device::write_texture_region(texture handle, const texture_write_region& region, const void* data, size_t size)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->object_id == 0 || record->target != GL_TEXTURE_2D)
        {
            LOG_WRN("write_texture_region: invalid 2D texture handle");
            return false;
        }
        if (region.mip_level >= record->mip_levels)
        {
            LOG_WRN("write_texture_region: level %u of a %u-level texture", region.mip_level, record->mip_levels);
            return false;
        }
        const uint32_t level_width = level_extent(record->width, region.mip_level);
        const uint32_t level_height = level_extent(record->height, region.mip_level);
        const uint64_t right = static_cast<uint64_t>(region.x) + region.width;
        const uint64_t bottom = static_cast<uint64_t>(region.y) + region.height;
        if (region.width == 0 || region.height == 0 || right > level_width || bottom > level_height)
        {
            LOG_WRN("write_texture_region: %ux%u at %u,%u does not fit level %u (%ux%u)",
                    region.width,
                    region.height,
                    region.x,
                    region.y,
                    region.mip_level,
                    level_width,
                    level_height);
            return false;
        }
        if (!upload_fits(
                "write_texture_region", size, static_cast<uint64_t>(region.width) * region.height, record->format))
        {
            return false;
        }
        const auto fmt = to_gl_texture_format(record->format);
        GL_CHECK(glTextureSubImage2D(record->object_id,
                                     static_cast<GLint>(region.mip_level),
                                     static_cast<GLint>(region.x),
                                     static_cast<GLint>(region.y),
                                     static_cast<GLsizei>(region.width),
                                     static_cast<GLsizei>(region.height),
                                     fmt.upload_format,
                                     fmt.upload_type,
                                     data));
        return true;
    }

    void gl_device::write_texture_3d(texture handle, const void* data, size_t size)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->object_id == 0 || record->target != GL_TEXTURE_3D)
        {
            LOG_WRN("write_texture_3d: invalid 3D texture handle");
            return;
        }
        if (!upload_fits("write_texture_3d",
                         size,
                         static_cast<uint64_t>(record->width) * record->height * record->depth,
                         record->format))
        {
            return;
        }
        const auto fmt = to_gl_texture_format(record->format);
        GL_CHECK(glTextureSubImage3D(record->object_id,
                                     0,
                                     0,
                                     0,
                                     0,
                                     static_cast<GLsizei>(record->width),
                                     static_cast<GLsizei>(record->height),
                                     static_cast<GLsizei>(record->depth),
                                     fmt.upload_format,
                                     fmt.upload_type,
                                     data));
    }

    void gl_device::write_cube_face(texture handle, cube_face face, const void* data, size_t size)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->object_id == 0 || record->target != GL_TEXTURE_CUBE_MAP)
        {
            LOG_WRN("write_cube_face: invalid cube texture handle");
            return;
        }
        if (!upload_fits(
                "write_cube_face", size, static_cast<uint64_t>(record->width) * record->height, record->format))
        {
            return;
        }
        // The named upload addresses a cube map like a six-layer array:
        // the face is the z offset, in the +X, -X, +Y, -Y, +Z, -Z order
        // of the GL face enums.
        const GLint layer = static_cast<GLint>(to_gl_cube_face(face) - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
        const auto fmt = to_gl_texture_format(record->format);
        GL_CHECK(glTextureSubImage3D(record->object_id,
                                     0,
                                     0,
                                     0,
                                     layer,
                                     static_cast<GLsizei>(record->width),
                                     static_cast<GLsizei>(record->height),
                                     1,
                                     fmt.upload_format,
                                     fmt.upload_type,
                                     data));
    }

    void gl_device::generate_mipmaps(texture handle)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->object_id == 0)
        {
            return;
        }
        if (record->mip_levels <= 1)
        {
            LOG_WRN("generate_mipmaps: texture was created without mipmaps");
            return;
        }
        // Format-agnostic: an rgba8_srgb texture carries its encoding in
        // its GL_SRGB8_ALPHA8 storage, so the driver derives the chain
        // from that storage (desktop drivers filter the decoded values)
        // and no CPU pre-pass is needed for a gamma-correct chain.
        GL_CHECK(glGenerateTextureMipmap(record->object_id));
    }

    sampler gl_device::create_sampler(const sampler_descriptor& descriptor)
    {
        // A real sampler object. Bound with glBindSampler to the unit
        // its binding number names, it overrides the state baked onto
        // whichever texture is on that unit, so one texture can be
        // sampled two ways and depth-comparison lookups are reachable.
        gl_sampler record{};
        record.descriptor = descriptor;

        glCreateSamplers(1, &record.object_id);
        const GLuint id = record.object_id;
        glSamplerParameteri(
            id, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(to_gl_min_filter(descriptor.min_filter, descriptor.mipmap)));
        glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(to_gl_mag_filter(descriptor.mag_filter)));
        glSamplerParameteri(id, GL_TEXTURE_WRAP_S, static_cast<GLint>(to_gl_address_mode(descriptor.address_u)));
        glSamplerParameteri(id, GL_TEXTURE_WRAP_T, static_cast<GLint>(to_gl_address_mode(descriptor.address_v)));
        glSamplerParameteri(id, GL_TEXTURE_WRAP_R, static_cast<GLint>(to_gl_address_mode(descriptor.address_w)));
        glSamplerParameteri(
            id, GL_TEXTURE_COMPARE_MODE, descriptor.compare_enabled ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
        GL_CHECK(
            glSamplerParameteri(id, GL_TEXTURE_COMPARE_FUNC, static_cast<GLint>(to_gl_compare(descriptor.compare))));

        sampler h{};
        h.id = m_samplers.insert(record);
        return h;
    }

    void gl_device::destroy(sampler handle)
    {
        if (auto* record = m_samplers.lookup(handle.id))
        {
            if (record->object_id != 0)
            {
                glDeleteSamplers(1, &record->object_id);
            }
            m_samplers.remove(handle.id);
            invalidate_state_cache();
        }
    }
} // namespace rendering_engine::gpu::backend::opengl
