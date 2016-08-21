#pragma once

#include "OpenGL\Typedef.hpp"
#include "OpenGL\VertexBuffer.hpp"
#include "OpenGL\VertexArray.hpp"

#include "Mesh.hpp"

class Geometry
{
public:
	Geometry(Mesh& mesh);
	~Geometry(void) {}

	void Draw(void);

private:
	Geometry(Geometry&);
	Geometry& operator=(Geometry&);

	OpenGL::VertexBuffer m_VertexBufferObject;
	OpenGL::VertexArray m_VertexArrayObject;
	uint32_t m_DrawCount;
};
