/**
 * Copyright (c) 2015-2025 Tomislav Radanovic
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

#include <rendering_engine/opengl/opengl.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/window.hpp>

#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/renderers/basic_renderer.hpp>
#include <rendering_engine/renderers/overlay_renderer.hpp>

#include <event_engine/event_engine.hpp>

#include <infrastructure/log.hpp>
#include <rendering_engine/renderables/premade_3d/cube.hpp>

void rendering_engine::context::init()
{
    LOG_INF("Init Rendering Engine");

    rendering_engine::window::get_instance().init();
    rendering_engine::opengl::context::get_instance().init();

    rendering_engine::opengl::context::get_instance().enable(rendering_engine::opengl::capability::cull_face);
    rendering_engine::opengl::context::get_instance().enable(rendering_engine::opengl::capability::depth_test);
    rendering_engine::opengl::context::get_instance().enable(rendering_engine::opengl::capability::blend);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    LOG_INF("Rendering Engine: enabled cull_face, depth_test, blend (SRC_ALPHA / ONE_MINUS_SRC_ALPHA)");

    rendering_engine::renderers::basic_renderer::get_instance();
    rendering_engine::renderers::overlay_renderer::get_instance();
    LOG_INF("Rendering Engine: basic_renderer and overlay_renderer constructed");
}

void rendering_engine::context::quit()
{
    rendering_engine::opengl::context::get_instance().quit();
    rendering_engine::window::get_instance().quit();

    LOG_INF("Quit Rendering Engine");
}

void rendering_engine::context::render()
{
    rendering_engine::window::get_instance().clear();

    if (rendering_engine::camera::get_current_camera() != nullptr)
    {
        rendering_engine::renderers::basic_renderer::get_instance().start_renderer();
        rendering_engine::renderers::basic_renderer::get_instance().setup_camera();
        event_engine::context::get_instance().broadcast(event_engine::render_scene());
        rendering_engine::renderers::basic_renderer::get_instance().stop_renderer();
    }

    rendering_engine::opengl::context::get_instance().disable(rendering_engine::opengl::capability::depth_test);
    rendering_engine::renderers::overlay_renderer::get_instance().start_renderer();
    event_engine::context::get_instance().broadcast(event_engine::render_ui());
    rendering_engine::renderers::overlay_renderer::get_instance().stop_renderer();
    rendering_engine::opengl::context::get_instance().enable(rendering_engine::opengl::capability::depth_test);
}
