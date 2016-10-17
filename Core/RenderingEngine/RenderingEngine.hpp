#pragma once

#include <Utilities\Singleton.hpp>
#include <Utilities\Exception.hpp>

// Global settings
#include <Control\Settings.hpp>

#include "OpenGL\OpenGL.hpp"
#include "Camera.hpp"
#include "Model.hpp"
#include "Geometry.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "Renderer.hpp"
#include "StandardRenderer\StandardRenderer.hpp"

class RenderingEngine : public Singleton<RenderingEngine>
{
	friend Singleton<RenderingEngine>;
	RenderingEngine(void) : 
		m_IsInit{ false },
		m_GlContext{ OpenGL::Context::GetInstance() }
	{}

public:
	void Init(void);
	void Quit(void);

	void RenderScene(void);

private:
	bool m_IsInit;
	OpenGL::Context* m_GlContext;
	std::vector<Model*> m_Models; // This should be a tree in the future
};
