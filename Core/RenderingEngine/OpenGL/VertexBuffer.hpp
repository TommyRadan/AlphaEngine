#pragma once

#include "GLEW\glew.h"

namespace OpenGL
{
	class VertexBuffer
	{
	public:
		VertexBuffer(void);
		~VertexBuffer(void);

		const GLuint Handle(void) const;

		void Data(const void* data, const size_t length, const BufferUsage usage);
		void SubData(const void* data, const size_t offset, const size_t length);

		void GetSubData(void* data, const size_t offset, const size_t length);

	private:
		GLuint m_ObjectID;
	};
}
