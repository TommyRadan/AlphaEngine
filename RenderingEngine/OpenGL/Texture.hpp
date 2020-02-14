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

#include <RenderingEngine/OpenGL/Typedef.hpp>

namespace RenderingEngine
{
	namespace OpenGL
	{
		class Texture
		{
            friend class Context;
            friend class Framebuffer;
			Texture();
			~Texture();

            Texture(const Texture&) = delete;
            Texture(const Texture&&) = delete;
            const Texture& operator=(const Texture&) = delete;
            const Texture&& operator=(const Texture&&) = delete;

        public:
			const uint32_t Handle() const;

			void Image2D(const void* data,
						 DataType type,
                         Format format,
                         uint32_t width,
                         uint32_t height,
                         InternalFormat internalFormat);

			void SetWrappingS(Wrapping s);
			void SetWrappingT(Wrapping t);
			void SetWrappingR(Wrapping r);

			void SetFilters(Filter min, Filter mag);

			void GenerateMipmaps();

		private:
			unsigned int m_ObjectID;
		};
	}
}
