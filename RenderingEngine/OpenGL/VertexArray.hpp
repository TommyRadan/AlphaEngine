#pragma once

#include "Typedef.hpp"

#include "VertexBuffer.hpp"

namespace OpenGL
{
	class VertexArray
	{
	public:
		VertexArray(void);
		~VertexArray(void);

		const unsigned int Handle(void) const;

		void BindAttribute(
			const Attribute& attribute, 
			const VertexBuffer& buffer, 
			Type type, 
			unsigned int count, 
			unsigned int stride, 
			unsigned long long offset
		);

		void BindElements(const VertexBuffer& elements);

	private:
		VertexArray(const VertexArray& other);
		const VertexArray& operator=(const VertexArray& other);

		unsigned int m_ObjectID;
	};
}

