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

#include "api/game_module.hpp"
#include "api/log.hpp"
#include "api/time.hpp"

#include <rendering_engine/renderables/premade_3d/cube.hpp>

#include <rendering_engine/opengl/opengl.hpp>

#include <memory>

static std::unique_ptr<rendering_engine::cube> cube;

float rotation = 0.0f;
float rotation_speed = 3.14f / 2;

static void on_engine_start(const event_engine::event& event)
{
    cube = std::make_unique<rendering_engine::cube>();
    cube->upload();
}

static void on_engine_stop(const event_engine::event& event)
{
    cube.reset();
}

static void on_frame(const event_engine::event& event)
{
    rotation += rotation_speed * (static_cast<float>(get_delta_time()) / 1000);
    cube->transform.set_rotation(glm::vec3{0.f, 0.f, rotation});
}

static void on_render_scene(const event_engine::event& event)
{
    cube->render();
}

GAME_MODULE()
{
    struct game_module_info info;
    info.on_engine_start = on_engine_start;
    info.on_engine_stop = on_engine_stop;
    info.on_render_scene = on_render_scene;
    info.on_frame = on_frame;
    register_game_module(info);
    return true;
}
