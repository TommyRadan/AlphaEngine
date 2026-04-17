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

#include <cstddef>

#include <rendering_engine/opengl/typedef.hpp>

namespace rendering_engine
{
    namespace opengl
    {
        class vertex_buffer
        {
            friend class context;
            vertex_buffer();
            ~vertex_buffer();

            vertex_buffer(const vertex_buffer& other) = delete;
            vertex_buffer(const vertex_buffer&& other) = delete;
            const vertex_buffer& operator=(const vertex_buffer& other) = delete;
            const vertex_buffer&& operator=(const vertex_buffer&& other) = delete;

        public:
            const unsigned int handle() const;

            void data(const void* data, size_t length, buffer_usage usage);
            void element_data(const void* data, size_t length, buffer_usage usage);
            void sub_data(const void* data, size_t offset, size_t length);

            void get_sub_data(void* data, size_t offset, size_t length);

        private:
            unsigned int m_object_id;
        };
    } // namespace opengl
} // namespace rendering_engine
