#pragma once

#include <RenderingEngine/OpenGL/Typedef.hpp>
#include <RenderingEngine/OpenGL/Shader.hpp>

#include <Mathematics/glm.hpp>
#include <string>

namespace RenderingEngine
{
	namespace OpenGL
	{
		struct Program
		{
			Program();
			~Program();

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
		};
	}
}
