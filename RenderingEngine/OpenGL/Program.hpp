#pragma once

#include "Typedef.hpp"
#include "Shader.hpp"

#include <Mathematics/Math.hpp>

#include <string>

namespace OpenGL
{
	class Program
	{
	public:
		Program(void);
		~Program(void);

		const unsigned int Handle(void) const;

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
		void SetUniform(const Uniform& uniform, const Math::Vector2& value);
		void SetUniform(const Uniform& uniform, const Math::Vector3& value);
		void SetUniform(const Uniform& uniform, const Math::Vector4& value);
		void SetUniform(const Uniform& uniform, const float* values, unsigned int count);
		void SetUniform(const Uniform& uniform, const Math::Vector2* values, unsigned int count);
		void SetUniform(const Uniform& uniform, const Math::Vector3* values, unsigned int count);
		void SetUniform(const Uniform& uniform, const Math::Vector4* values, unsigned int count);
		void SetUniform(const Uniform& uniform, const Math::Matrix3& value);
		void SetUniform(const Uniform& uniform, const Math::Matrix4& value);

	private:
		unsigned int m_ObjectID;
	};
}
