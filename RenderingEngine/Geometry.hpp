#pragma once

#include "MeshLoading/Mesh.hpp"

// TODO: Follow rule of five
struct Geometry
{
	Geometry(void);
	~Geometry(void);

	void UploadMesh(const Mesh& mesh);
	void ReleaseData(void);

	void Draw(void);

private:
	Geometry(Geometry&);
	Geometry(Geometry&&);
	Geometry& operator=(Geometry&);
	Geometry& operator=(Geometry&&);

	bool m_DataUploaded;

	void* m_VertexBufferObject;
	void* m_VertexArrayObject;
	unsigned int m_DrawCount;
};
