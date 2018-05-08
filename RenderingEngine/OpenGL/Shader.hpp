#pragma once

#include <RenderingEngine/OpenGL/Typedef.hpp>

#include <exception>
#include <string>

namespace RenderingEngine
{
	namespace OpenGL
	{
		struct Shader
		{
			explicit Shader(ShaderType type);
			~Shader();

			const GLuint Handle() const;

			void Source(const std::string& code);
			void Compile();

			std::string GetInfoLog();

		private:
			GLuint m_ObjectID;
		};
	}
}
