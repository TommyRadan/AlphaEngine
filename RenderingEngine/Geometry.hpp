#pragma once

// OpenGL
#include "OpenGL/OpenGL.hpp"

// Mesh container
#include "Vertex.hpp"
#include "Mesh.hpp"

class Geometry
{
public:
	Geometry(void);
	~Geometry(void);

	void UploadMesh(const Mesh& mesh);
	void ReleaseData(void);

	void Draw(void);
	VertexFormat GetVertexFormat(void);

private:
	Geometry(Geometry&);
	Geometry& operator=(Geometry&);

	bool m_DataUploaded;

	OpenGL::VertexBuffer* m_VertexBufferObject;
	OpenGL::VertexArray* m_VertexArrayObject;
	VertexFormat m_VertexFormat;
	unsigned int m_DrawCount;
};
