#pragma once

#include "Typedef.hpp"

#include "VertexBuffer.hpp"

namespace OpenGL
{
	class VertexArray
	{
	public:
		VertexArray();
		VertexArray(const VertexArray& other);

		~VertexArray();

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

