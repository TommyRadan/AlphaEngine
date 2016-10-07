#pragma once

#include "Typedef.hpp"

#include "VertexBuffer.hpp"

namespace OpenGL
{
	class VertexArray
	{
	public:
		VertexArray(void);
		VertexArray(const VertexArray& other);

		~VertexArray(void);

		const GLuint Handle(void) const;
		const VertexArray& operator=(const VertexArray& other);

		void BindAttribute(
			const Attribute& attribute, 
			const VertexBuffer& buffer, 
			Type type, 
			unsigned int count, 
			unsigned int stride, 
			intptr_t offset
		);

		void BindElements(const VertexBuffer& elements);

	private:
		GLuint m_ObjectID;
	};
}

