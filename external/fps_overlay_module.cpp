/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
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

#include <infrastructure/log.hpp>
#include <rendering_engine/renderables/premade_2d/label.hpp>
#include <rendering_engine/util/font.hpp>

#include <exception>
#include <memory>
#include <string>

static constexpr const char* FONT_PATH = "C:/Windows/Fonts/consola.ttf";
static constexpr float FONT_SIZE = 0.08f;
static constexpr float TOP_Y = 0.92f;
static constexpr float RIGHT_MARGIN = 0.02f;
static constexpr double UPDATE_INTERVAL_MS = 250.0;

static std::unique_ptr<rendering_engine::util::font> font;
static std::unique_ptr<rendering_engine::label> fps_label;
static double time_since_update_ms = 0.0;

static void reposition_top_right()
{
    const float x = 1.0f - fps_label->get_width() - RIGHT_MARGIN;
    fps_label->set_position(infrastructure::math::vec3{x, TOP_Y, 0.0f});
}

static void on_engine_start(const event_engine::event& event)
{
    try
    {
        font = std::make_unique<rendering_engine::util::font>(FONT_PATH, 64.0f);
        fps_label = std::make_unique<rendering_engine::label>(font.get(), FONT_SIZE, "FPS: ---");
        fps_label->upload();
        reposition_top_right();
    }
    catch (const std::exception& e)
    {
        LOG_ERR("fps_overlay_module: failed to initialize (%s)", e.what());
        font.reset();
        fps_label.reset();
    }
}

static void on_engine_stop(const event_engine::event& event)
{
    fps_label.reset();
    font.reset();
}

static void on_frame(const event_engine::event& event)
{
    if (!fps_label)
    {
        return;
    }

    time_since_update_ms += get_delta_time();
    if (time_since_update_ms < UPDATE_INTERVAL_MS)
    {
        return;
    }
    time_since_update_ms = 0.0;

    const int fps = static_cast<int>(get_current_fps());
    fps_label->set_text(std::string{"FPS: "} + std::to_string(fps));
    reposition_top_right();
}

static void on_render_ui(const event_engine::event& event)
{
    if (!fps_label)
    {
        return;
    }
    fps_label->render();
}

GAME_MODULE()
{
    LOG_INF("Registering external module: fps_overlay_module");
    struct game_module_info info;
    info.on_engine_start = on_engine_start;
    info.on_engine_stop = on_engine_stop;
    info.on_frame = on_frame;
    info.on_render_ui = on_render_ui;
    register_game_module(info);
    return true;
}
