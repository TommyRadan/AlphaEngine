#include "RenderingEngine.hpp"
#include "StandardRenderer/StandardRenderer.hpp"

RenderingEngine::Context::Context(void)
{
	m_IsInit = false;
}

void RenderingEngine::Context::Init(void)
{
	if (m_IsInit) {
		throw Exception("RenderingEngine::Init called twice!");
	}

	OpenGL::Context::GetInstance()->Init();
	StandardRenderer::GetInstance()->Init();

	OpenGL::Context::GetInstance()->Enable(OpenGL::Capability::DepthTest);

	m_IsInit = true;
}

void RenderingEngine::Context::Quit(void)
{
	if (!m_IsInit) {
		throw Exception("RenderingEngine::Quit called before RenderingEngine::Init!");
	}

	StandardRenderer::GetInstance()->Quit();
	OpenGL::Context::GetInstance()->Quit();

	m_IsInit = false;
}
