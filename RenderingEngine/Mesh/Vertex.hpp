#pragma once

#include <Mathematics/glm.hpp>

namespace RenderingEngine
{
	struct Vertex
	{
		glm::vec3 Pos;
		glm::vec2 Tex;
		glm::vec3 Normal;
	};
}
