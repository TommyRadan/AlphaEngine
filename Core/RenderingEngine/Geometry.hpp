#pragma once

#include "OpenGL\OpenGL.hpp"

#include "Mesh.hpp"

class Geometry
{
public:
	Geometry(const Mesh& mesh);
	~Geometry(void) {}

	void Draw(void);

private:
	Geometry(Geometry&);
	Geometry& operator=(Geometry&);

	OpenGL::VertexBuffer m_VertexBufferObject;
	OpenGL::VertexArray m_VertexArrayObject;
	uint32_t m_DrawCount;
};
