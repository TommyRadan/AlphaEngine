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
 * @brief @c gl_device member functions that manage GL textures and the
 *        forward-compat sampler resource: @c create_texture,
 *        @c create_sampler, @c destroy(texture), @c destroy(sampler),
 *        @c write_texture, @c write_cube_face, @c generate_mipmaps.
 */

#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>

#include <glad/gl.h>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_translate.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    namespace
    {
        void apply_sampler_state(GLenum target,
                                 const filter_mode min,
                                 const filter_mode mag,
                                 const mipmap_mode mip,
                                 const address_mode u,
                                 const address_mode v,
                                 const address_mode w)
        {
            glTexParameteri(target, GL_TEXTURE_MIN_FILTER, to_gl_min_filter(min, mip));
            glTexParameteri(target, GL_TEXTURE_MAG_FILTER, to_gl_mag_filter(mag));
            glTexParameteri(target, GL_TEXTURE_WRAP_S, to_gl_address_mode(u));
            glTexParameteri(target, GL_TEXTURE_WRAP_T, to_gl_address_mode(v));
            glTexParameteri(target, GL_TEXTURE_WRAP_R, to_gl_address_mode(w));
        }
    } // namespace

    texture gl_device::create_texture(const texture_descriptor& descriptor)
    {
        gl_texture record{};
        record.target = to_gl_texture_target(descriptor.dimension);
        record.format = descriptor.format;
        record.width = descriptor.width;
        record.height = descriptor.height;
        record.depth = descriptor.dimension == texture_dimension::d3 ? descriptor.depth : 1u;
        record.mipmaps = descriptor.mipmaps;

        glGenTextures(1, &record.object_id);
        glBindTexture(record.target, record.object_id);

        apply_sampler_state(record.target,
                            descriptor.min_filter,
                            descriptor.mag_filter,
                            descriptor.mipmap_filter,
                            descriptor.address_u,
                            descriptor.address_v,
                            descriptor.address_w);

        // Allocate storage with null data. The caller fills
        // each level via @ref write_texture, @ref write_texture_3d,
        // or @ref write_cube_face. 2D and 3D textures get a
        // single level-0 allocation; cube maps get all six
        // faces so each can be uploaded independently.
        const auto fmt = to_gl_texture_format(descriptor.format);
        if (record.target == GL_TEXTURE_2D)
        {
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         fmt.internal_format,
                         static_cast<GLsizei>(descriptor.width),
                         static_cast<GLsizei>(descriptor.height),
                         0,
                         fmt.upload_format,
                         fmt.upload_type,
                         nullptr);
        }
        else if (record.target == GL_TEXTURE_3D)
        {
            glTexImage3D(GL_TEXTURE_3D,
                         0,
                         fmt.internal_format,
                         static_cast<GLsizei>(descriptor.width),
                         static_cast<GLsizei>(descriptor.height),
                         static_cast<GLsizei>(record.depth),
                         0,
                         fmt.upload_format,
                         fmt.upload_type,
                         nullptr);
        }
        else
        {
            for (int face = 0; face < 6; ++face)
            {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                             0,
                             fmt.internal_format,
                             static_cast<GLsizei>(descriptor.width),
                             static_cast<GLsizei>(descriptor.height),
                             0,
                             fmt.upload_format,
                             fmt.upload_type,
                             nullptr);
            }
        }

        glBindTexture(record.target, 0);

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
        }
    }

    void gl_device::write_texture(texture handle, const void* data, size_t /*size*/)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->object_id == 0 || record->target != GL_TEXTURE_2D)
        {
            LOG_WRN("write_texture: invalid 2D texture handle");
            return;
        }
        const auto fmt = to_gl_texture_format(record->format);
        glBindTexture(GL_TEXTURE_2D, record->object_id);
        glTexSubImage2D(GL_TEXTURE_2D,
                        0,
                        0,
                        0,
                        static_cast<GLsizei>(record->width),
                        static_cast<GLsizei>(record->height),
                        fmt.upload_format,
                        fmt.upload_type,
                        data);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void gl_device::write_texture_3d(texture handle, const void* data, size_t /*size*/)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->object_id == 0 || record->target != GL_TEXTURE_3D)
        {
            LOG_WRN("write_texture_3d: invalid 3D texture handle");
            return;
        }
        const auto fmt = to_gl_texture_format(record->format);
        glBindTexture(GL_TEXTURE_3D, record->object_id);
        glTexSubImage3D(GL_TEXTURE_3D,
                        0,
                        0,
                        0,
                        0,
                        static_cast<GLsizei>(record->width),
                        static_cast<GLsizei>(record->height),
                        static_cast<GLsizei>(record->depth),
                        fmt.upload_format,
                        fmt.upload_type,
                        data);
        glBindTexture(GL_TEXTURE_3D, 0);
    }

    void gl_device::write_cube_face(texture handle, cube_face face, const void* data, size_t /*size*/)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->object_id == 0 || record->target != GL_TEXTURE_CUBE_MAP)
        {
            LOG_WRN("write_cube_face: invalid cube texture handle");
            return;
        }
        const auto fmt = to_gl_texture_format(record->format);
        glBindTexture(GL_TEXTURE_CUBE_MAP, record->object_id);
        glTexSubImage2D(to_gl_cube_face(face),
                        0,
                        0,
                        0,
                        static_cast<GLsizei>(record->width),
                        static_cast<GLsizei>(record->height),
                        fmt.upload_format,
                        fmt.upload_type,
                        data);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    void gl_device::generate_mipmaps(texture handle)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->object_id == 0)
        {
            return;
        }
        glBindTexture(record->target, record->object_id);
        glGenerateMipmap(record->target);
        glBindTexture(record->target, 0);
    }

    sampler gl_device::create_sampler(const sampler_descriptor& descriptor)
    {
        // The GL backend currently expresses sampler state on
        // the texture itself (set at create time via
        // @c texture_descriptor). Stand-alone samplers exist
        // in the API for forward-compatibility with explicit
        // binding-model backends; on GL they are recorded
        // here and treated as no-ops at bind time.
        gl_sampler record{};
        record.descriptor = descriptor;
        sampler h{};
        h.id = m_samplers.insert(record);
        return h;
    }

    void gl_device::destroy(sampler handle)
    {
        m_samplers.remove(handle.id);
    }
} // namespace rendering_engine::gpu::backend::opengl
