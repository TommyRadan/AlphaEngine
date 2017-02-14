#include "RenderingEngine.hpp"

void RenderingEngine::Init(void)
{
	if (m_IsInit) {
		throw Exception("RenderingEngine::Init called twice!");
	}

	m_GlContext->Init();
	//StandardRenderer::GetInstance()->Init();

	m_GlContext->Enable(OpenGL::Capability::DepthTest);
}

void RenderingEngine::Quit(void)
{
	if (m_IsInit) {
		throw Exception("RenderingEngine::Quit called before RenderingEngine::Init!");
	}

	//StandardRenderer::GetInstance()->Quit();
	m_GlContext->Quit();
}

void RenderingEngine::RenderScene(void)
{
	OpenGL::Context::GetInstance()->ClearColor(Color(52, 152, 219, 255));
	OpenGL::Context::GetInstance()->Clear();

	for (auto& m : m_Models) {
		m->Render(StandardRenderer::GetInstance());
	}
}
