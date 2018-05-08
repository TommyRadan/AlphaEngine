#pragma once

#include <cstddef>

#include <RenderingEngine/OpenGL/Typedef.hpp>

namespace RenderingEngine
{
	namespace OpenGL
	{
		struct VertexBuffer
		{
			VertexBuffer();
			~VertexBuffer();

			const unsigned int Handle() const;

			void Data(const void* data, size_t length, BufferUsage usage);
			void SubData(const void* data, size_t offset, size_t length);

			void GetSubData(void* data, size_t offset, size_t length);

		private:
			unsigned int m_ObjectID;
		};
	}
}
