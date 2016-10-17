#include "RenderingEngine.hpp"

void RenderingEngine::Init(void)
{
	if (m_IsInit) {
		throw Exception("RenderingEngine::Init called twice!");
	}

	m_GlContext->Init();
	// To avoid shader compilation error
	//StandardRenderer::GetInstance()->Init();
}

void RenderingEngine::Quit(void)
{
	if (m_IsInit) {
		throw Exception("RenderingEngine::Quit called before RenderingEngine::Init!");
	}

	// To avoid shader compilation error
	//StandardRenderer::GetInstance()->Quit();
	m_GlContext->Quit();
}

void RenderingEngine::RenderScene(void)
{
}
