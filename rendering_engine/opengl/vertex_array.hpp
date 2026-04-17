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
#include <rendering_engine/opengl/vertex_buffer.hpp>

namespace rendering_engine
{
    namespace opengl
    {
        class vertex_array
        {
            friend class context;
            vertex_array();
            ~vertex_array();

            vertex_array(const vertex_array& other) = delete;
            vertex_array(const vertex_array&& other) = delete;
            const vertex_array& operator=(const vertex_array& other) = delete;
            const vertex_array&& operator=(const vertex_array&& other) = delete;

        public:
            const uint32_t handle() const;

            void bind_attribute(const attribute& attribute,
                               const vertex_buffer& buffer,
                               type type,
                               unsigned int count,
                               unsigned int stride,
                               unsigned long long offset);

            void bind_elements(const vertex_buffer& elements);

        private:
            unsigned int m_object_id;
        };
    } // namespace opengl
} // namespace rendering_engine
