#pragma once

#include <RenderingEngine/OpenGL/Typedef.hpp>
#include <RenderingEngine/OpenGL/VertexBuffer.hpp>

namespace RenderingEngine
{
	namespace OpenGL
	{
		struct VertexArray
		{
			VertexArray();
			~VertexArray();

			VertexArray(const VertexArray& other) = delete;
			const VertexArray& operator=(const VertexArray& other) = delete;

			const uint32_t Handle() const;

			void BindAttribute(const Attribute& attribute,
							   const VertexBuffer& buffer,
							   Type type,
							   unsigned int count,
							   unsigned int stride,
							   unsigned long long offset);

			void BindElements(const VertexBuffer& elements);

		private:
			unsigned int m_ObjectID;
		};
	}
}
