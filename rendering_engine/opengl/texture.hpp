/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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
        class texture
        {
            friend class context;
            friend class framebuffer;
            texture();
            ~texture();

            texture(const texture&) = delete;
            texture(const texture&&) = delete;
            const texture& operator=(const texture&) = delete;
            const texture&& operator=(const texture&&) = delete;

        public:
            const uint32_t handle() const;

            void image2_d(const void* data,
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
