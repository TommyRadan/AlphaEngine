#pragma once

// OpenGL
#include "Typedef.hpp"
#include "Shader.hpp"

// Mathematics
#include <Mathematics\glm.hpp>

// Standard Library
#include <string>

namespace OpenGL
{
	class Program
	{
	public:
		Program(void);
		~Program(void);

		const GLuint Handle(void) const;

		void Start(void);
		void Stop(void);

		void Attach(const Shader& shader);
		void Link(void);

		std::string GetInfoLog(void);

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
		void SetUniform(const Uniform& uniform, const Math::vec2& value);
		void SetUniform(const Uniform& uniform, const Math::vec3& value);
		void SetUniform(const Uniform& uniform, const Math::vec4& value);
		void SetUniform(const Uniform& uniform, const float* values, unsigned int count);
		void SetUniform(const Uniform& uniform, const Math::vec2* values, unsigned int count);
		void SetUniform(const Uniform& uniform, const Math::vec3* values, unsigned int count);
		void SetUniform(const Uniform& uniform, const Math::vec4* values, unsigned int count);
		void SetUniform(const Uniform& uniform, const Math::mat3& value);
		void SetUniform(const Uniform& uniform, const Math::mat4& value);

	private:
		GLuint m_ObjectID;
	};
}
