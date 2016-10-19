#pragma once

#include <Utilities\Singleton.hpp>
#include <Utilities\Exception.hpp>
#include <Utilities\Mesh.hpp>

// Global settings
#include <Control\Settings.hpp>

#include "OpenGL\OpenGL.hpp"
#include "Geometry.hpp"
#include "Material.hpp"
#include "Renderer.hpp"
#include "Camera.hpp"
#include "Model.hpp"

// Specific renderers
#include "StandardRenderer\StandardRenderer.hpp"

class RenderingEngine : 
	public Singleton<RenderingEngine>
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
