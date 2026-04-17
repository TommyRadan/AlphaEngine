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

#include <rendering_engine/opengl/texture.hpp>
#include <rendering_engine/opengl/typedef.hpp>

namespace rendering_engine
{
    namespace opengl
    {
        class framebuffer
        {
            friend class context;
            framebuffer(uint32_t width, uint32_t height, uint8_t color = 32u, uint8_t depth = 24u);
            ~framebuffer();

            framebuffer(const framebuffer&) = delete;
            framebuffer(const framebuffer&&) = delete;
            const framebuffer& operator=(const framebuffer&) = delete;
            const framebuffer&& operator=(const framebuffer&&) = delete;

        public:
            const uint32_t handle() const;

            const texture& get_texture() const;
            const texture& get_depth_texture() const;

        private:
            uint32_t m_object_id;
            texture m_color_texture;
            texture m_depth_texture;
        };
    } // namespace opengl
} // namespace rendering_engine
