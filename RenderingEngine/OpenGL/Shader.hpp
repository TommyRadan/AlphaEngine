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

#include <string>

namespace RenderingEngine
{
	namespace OpenGL
	{
		class Shader
		{
            friend class Context;
			explicit Shader(ShaderType type);
			~Shader();

            Shader(const Shader&) = delete;
            Shader(const Shader&&) = delete;
            const Shader& operator=(const Shader&) = delete;
            const Shader&& operator=(const Shader&&) = delete;

        public:
			const GLuint Handle() const;

			void Source(const std::string& code);
			void Compile();

			std::string GetInfoLog();

		private:
			std::string m_Code;
			GLuint m_ObjectID;
		};
	}
}
