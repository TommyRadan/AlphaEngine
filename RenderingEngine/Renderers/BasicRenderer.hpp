#pragma once

#include <RenderingEngine/Renderers/Renderer.hpp>

namespace RenderingEngine
{
	namespace Renderers
	{
		class BasicRenderer : public Renderer
		{
			BasicRenderer();

		public:
			static BasicRenderer* GetInstance();

			~BasicRenderer();
		};
	}
}
