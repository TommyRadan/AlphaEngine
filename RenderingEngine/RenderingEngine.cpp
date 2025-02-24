/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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
#include <EventEngine/Dispatch.hpp>

#include <RenderingEngine/Renderers/BasicRenderer.hpp>
#include <RenderingEngine/Renderers/OverlayRenderer.hpp>
#include <RenderingEngine/Camera/Camera.hpp>

#include <Infrastructure/Log.hpp>
#include <RenderingEngine/Renderables/Premade3D/Cube.hpp>

void RenderingEngine::Context::Init()
{
    LOG_INFO("Init Rendering Engine");

    RenderingEngine::Window::get_instance().Init();
    RenderingEngine::OpenGL::Context::get_instance().Init();

    RenderingEngine::OpenGL::Context::get_instance().Enable(RenderingEngine::OpenGL::Capability::CullFace);
    RenderingEngine::OpenGL::Context::get_instance().Enable(RenderingEngine::OpenGL::Capability::DepthTest);
    RenderingEngine::OpenGL::Context::get_instance().Enable(RenderingEngine::OpenGL::Capability::Blend);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    RenderingEngine::Renderers::BasicRenderer::get_instance();
    RenderingEngine::Renderers::OverlayRenderer::get_instance();
}

void RenderingEngine::Context::Quit()
{
    RenderingEngine::OpenGL::Context::get_instance().Quit();
    RenderingEngine::Window::get_instance().Quit();

    LOG_INFO("Quit Rendering Engine");
}

void RenderingEngine::Context::Render()
{
    RenderingEngine::Window::get_instance().Clear();

    if (RenderingEngine::Camera::GetCurrentCamera() != nullptr)
    {
        RenderingEngine::Renderers::BasicRenderer::get_instance().StartRenderer();
        RenderingEngine::Renderers::BasicRenderer::get_instance().SetupCamera();
        EventEngine::Dispatch::get_instance().DispatchOnRenderSceneCallback();
        RenderingEngine::Renderers::BasicRenderer::get_instance().StopRenderer();
    }

    RenderingEngine::OpenGL::Context::get_instance().Disable(RenderingEngine::OpenGL::Capability::DepthTest);
    RenderingEngine::Renderers::OverlayRenderer::get_instance().StartRenderer();
    EventEngine::Dispatch::get_instance().DispatchOnRenderUiCallback();
    RenderingEngine::Renderers::OverlayRenderer::get_instance().StopRenderer();
    RenderingEngine::OpenGL::Context::get_instance().Enable(RenderingEngine::OpenGL::Capability::DepthTest);
}
