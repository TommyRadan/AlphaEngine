#pragma once

#include <cstddef>
#include "Typedef.hpp"

namespace OpenGL
{
	class VertexBuffer
	{
	public:
		VertexBuffer(void);
		~VertexBuffer(void);

		const unsigned int Handle(void) const;

		void Data(const void* data, const size_t length, const BufferUsage usage);
		void SubData(const void* data, const size_t offset, const size_t length);

		void GetSubData(void* data, const size_t offset, const size_t length);

	private:
		unsigned int m_ObjectID;
	};
}
