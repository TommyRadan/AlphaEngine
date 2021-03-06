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
#include <RenderingEngine/OpenGL/Shader.hpp>

#include <glm.hpp>
#include <string>
#include <map>

namespace RenderingEngine
{
	namespace OpenGL
	{
		class Program
		{
			friend class Context;
			Program();
			~Program();

			Program(const Program&) = delete;
			Program(const Program&&) = delete;
			const Program& operator=(const Program&) = delete;
			const Program&& operator=(const Program&&) = delete;

		public:
			const uint32_t Handle() const;

			void Start();
			void Stop();

			void Attach(const Shader& shader);
			void Link();

			std::string GetInfoLog();

			Attribute GetAttribute(const std::string& name);
			Uniform GetUniform(const std::string& name);

			template <typename T>
			void SetUniform(const std::string& name, const T& value)
			{
				this->SetUniform(this->GetUniform(name), value);
			}

			template <typename T>
			void SetUniform(const std::string& name, const T* values, unsigned int count)
			{
				this->SetUniform(this->GetUniform(name), values, count);
			}

			void SetUniform(const Uniform& uniform, int value);
			void SetUniform(const Uniform& uniform, float value);
			void SetUniform(const Uniform& uniform, const glm::vec2& value);
			void SetUniform(const Uniform& uniform, const glm::vec3& value);
			void SetUniform(const Uniform& uniform, const glm::vec4& value);
			void SetUniform(const Uniform& uniform, const float* values, unsigned int count);
			void SetUniform(const Uniform& uniform, const glm::vec2* values, unsigned int count);
			void SetUniform(const Uniform& uniform, const glm::vec3* values, unsigned int count);
			void SetUniform(const Uniform& uniform, const glm::vec4* values, unsigned int count);
			void SetUniform(const Uniform& uniform, const glm::mat3& value);
			void SetUniform(const Uniform& uniform, const glm::mat4& value);

		private:
			unsigned int m_ObjectID;
			std::map<std::string, Uniform> m_Uniforms;
			std::map<std::string, Attribute> m_Attributes;
		};
	}
}
