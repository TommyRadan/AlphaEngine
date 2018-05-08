#include <RenderingEngine/RenderingEngine.hpp>
#include <RenderingEngine/Window.hpp>
#include <RenderingEngine/OpenGL/OpenGL.hpp>

RenderingEngine::Context* RenderingEngine::Context::GetInstance()
{
    static Context* instance = nullptr;

    if (instance == nullptr)
    {
        instance = new Context;
    }

    return instance;
}

void RenderingEngine::Context::Init()
{
    RenderingEngine::Window::GetInstance()->Init();
    RenderingEngine::OpenGL::Context::GetInstance()->Init();

    RenderingEngine::OpenGL::Context::GetInstance()->Enable(RenderingEngine::OpenGL::Capability::CullFace);
    RenderingEngine::OpenGL::Context::GetInstance()->Enable(RenderingEngine::OpenGL::Capability::DepthTest);
}

void RenderingEngine::Context::Quit()
{
    RenderingEngine::OpenGL::Context::GetInstance()->Quit();
    RenderingEngine::Window::GetInstance()->Quit();
}

void RenderingEngine::Context::Render()
{
    RenderingEngine::Window::GetInstance()->Clear();

	/*
	 * TODO: Render!
	 */

    RenderingEngine::Window::GetInstance()->SwapBuffers();
}
