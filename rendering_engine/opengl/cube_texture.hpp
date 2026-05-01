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

#pragma once

#include <rendering_engine/opengl/typedef.hpp>

namespace rendering_engine
{
    namespace opengl
    {
        // Identifier for one of the six faces of a cube map. Values match
        // the GL constants so the wrapper can pass them through directly.
        enum class cube_face
        {
            positive_x = GL_TEXTURE_CUBE_MAP_POSITIVE_X,
            negative_x = GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
            positive_y = GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
            negative_y = GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
            positive_z = GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
            negative_z = GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
        };

        // Cube-map texture: a single GL texture object that holds six 2D
        // face images. Sampled in shaders by 3D direction vector
        // (samplerCube). Used for environment maps, sky boxes and — for
        // this engine — planet surfaces wrapped on a cubed sphere, where
        // it eliminates the polar singularity of an equirectangular layout.
        class cube_texture
        {
            friend class context;
            cube_texture();
            ~cube_texture();

            cube_texture(const cube_texture&) = delete;
            cube_texture(const cube_texture&&) = delete;
            const cube_texture& operator=(const cube_texture&) = delete;
            const cube_texture&& operator=(const cube_texture&&) = delete;

        public:
            const uint32_t handle() const;

            // Uploads pixel data to one of the six faces. Calling for all
            // six faces (with matching dimensions / formats) populates the
            // full cube map.
            void image2_d(cube_face face,
                          const void* data,
                          data_type type,
                          format format,
                          uint32_t width,
                          uint32_t height,
                          internal_format internal_format);

            void set_wrapping_s(wrapping s);
            void set_wrapping_t(wrapping t);
            void set_wrapping_r(wrapping r);

            void set_filters(filter min, filter mag);

            void generate_mipmaps();

        private:
            unsigned int m_object_id;
        };
    } // namespace opengl
} // namespace rendering_engine
