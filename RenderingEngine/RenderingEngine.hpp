#pragma once

#include <Utilities/Component.hpp>
#include <Utilities/Singleton.hpp>
#include <Utilities/Exception.hpp>
#include <Utilities/Color.hpp>

// Global settings
#include <Control/Settings.hpp>

#include "OpenGL/OpenGL.hpp"
#include "Geometry.hpp"
#include "Material.hpp"
#include "Renderer.hpp"
#include "Camera.hpp"
#include "Model.hpp"
#include "Mesh.hpp"

// Specific renderers
#include "StandardRenderer/StandardRenderer.hpp"

class RenderingEngine : 
	public Component,
	public Singleton<RenderingEngine>
{
	template<typename RenderingEngine>
	friend class Singleton;
	RenderingEngine(void) : 
		m_IsInit{ false },
		m_GlContext{ OpenGL::Context::GetInstance() }
	{}

public:
	void Init(void) override;
	void Quit(void) override;

	void RenderScene(void);

private:
	bool m_IsInit;
	OpenGL::Context* m_GlContext;
	std::vector<Model*> m_Models; // This should be a tree in the future
};
