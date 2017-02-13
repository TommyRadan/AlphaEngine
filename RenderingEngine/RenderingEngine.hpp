#pragma once

#include <Utilities/Component.hpp>
#include <Utilities/Exception.hpp>
#include <Utilities/Color.hpp>

#include <Control/Settings.hpp>

// TODO: Outside includer doesn't need to know about OpenGL
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
	public Component
{
	RenderingEngine(void) : 
		m_IsInit{ false },
		m_GlContext{ OpenGL::Context::GetInstance() }
	{}

public:
	static RenderingEngine* GetInstance(void)
	{
		static RenderingEngine* instance = nullptr;
		if(instance == nullptr) {
			instance = new RenderingEngine();
		}
		return instance;
	}

	void Init(void) override;
	void Quit(void) override;

	void RenderScene(void);

private:
	bool m_IsInit;
	OpenGL::Context* m_GlContext;
	std::vector<Model*> m_Models; // This should be a tree in the future
};
