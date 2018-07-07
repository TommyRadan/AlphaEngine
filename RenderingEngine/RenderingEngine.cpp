/**
 * Copyright (c) 2018 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <RenderingEngine/RenderingEngine.hpp>
#include <RenderingEngine/Window.hpp>
#include <RenderingEngine/OpenGL/OpenGL.hpp>
#include <RenderingEngine/Renderers/BasicRenderer.hpp>
#include <RenderingEngine/Camera.hpp>

#include <Infrastructure/Log.hpp>

RenderingEngine::Context* RenderingEngine::Context::GetInstance()
{
    static Context* instance = nullptr;

    if (instance == nullptr)
    {
        instance = new Context;
    }

    return instance;
}

RenderingEngine::Context::Context() :
    m_CurrentRenderer { nullptr }
{}

void RenderingEngine::Context::Init()
{
    LOG_INFO("Init Rendering Engine");

    RenderingEngine::Window::GetInstance()->Init();
    RenderingEngine::OpenGL::Context::GetInstance()->Init();

    RenderingEngine::OpenGL::Context::GetInstance()->Enable(RenderingEngine::OpenGL::Capability::CullFace);
    RenderingEngine::OpenGL::Context::GetInstance()->Enable(RenderingEngine::OpenGL::Capability::DepthTest);

    RenderingEngine::Renderers::BasicRenderer::GetInstance();

    m_CurrentRenderer = nullptr;
}

void RenderingEngine::Context::Quit()
{
    m_CurrentRenderer = nullptr;

    RenderingEngine::OpenGL::Context::GetInstance()->Quit();
    RenderingEngine::Window::GetInstance()->Quit();

    LOG_INFO("Quit Rendering Engine");
}

void RenderingEngine::Context::Render()
{
    RenderingEngine::Window::GetInstance()->Clear();

	/*
	 * TODO: Render!
	 */

    RenderingEngine::Window::GetInstance()->SwapBuffers();
}

RenderingEngine::Renderer* const RenderingEngine::Context::GetCurrentRenderer()
{
    return m_CurrentRenderer;
}
