#pragma once

// OpenGL
#include "Typedef.hpp"

// Standard Library
#include <exception>
#include <string>

#include <Utilities/Exception.hpp>

namespace OpenGL
{
	class Shader
	{
	public:
		Shader(const ShaderType type);
		~Shader(void);
		
		const GLuint Handle(void) const;
		
		void Source(const std::string& code);
		void Compile(void);

		std::string GetInfoLog(void);

	private:
		GLuint m_ObjectID;
	};
}

