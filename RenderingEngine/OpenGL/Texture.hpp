#pragma once

#include <RenderingEngine/OpenGL/Typedef.hpp>

namespace RenderingEngine
{
	namespace OpenGL
	{
		struct Texture
		{
			Texture();
			~Texture();

			const uint32_t Handle() const;

			void Image2D(const void* data, DataType type, Format format, uint32_t width, uint32_t height, InternalFormat internalFormat);

			void SetWrappingS(Wrapping s);
			void SetWrappingT(Wrapping t);
			void SetWrappingR(Wrapping r);

			void SetFilters(Filter min, Filter mag);

			void GenerateMipmaps();

		private:
			unsigned int m_ObjectID;
		};
	}
}
