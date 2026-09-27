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
#include <core/event_engine.hpp>
#include <core/log.hpp>
#include <core/math/vec2.hpp>
#include <rendering_engine/assets/asset_cache.hpp>
#include <rendering_engine/renderables/premade_2d/label.hpp>
#include <rendering_engine/renderables/premade_2d/pane.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <runtime/engine.hpp>

#include <cstdlib>
#include <exception>
#include <memory>

// A small quit button pinned to the bottom-right corner: a translucent pane
// that lights up under the cursor and quits when clicked, captioned "Esc"
// when ALPHAENGINE_UI_FONT names a TrueType font to set it in.
namespace
{
    namespace math = core::math;

    constexpr math::vec2 button_size{56.0f, 32.0f};
    constexpr math::vec2 button_margin{12.0f, 12.0f};
    constexpr rendering_engine::util::color idle_color{20, 20, 24, 160};
    constexpr rendering_engine::util::color hover_color{200, 60, 50, 220};

    std::unique_ptr<rendering_engine::pane> g_button;
    std::unique_ptr<rendering_engine::label> g_caption;
    bool g_hovered = false;

    bool is_over_button(float x, float y)
    {
        return g_button != nullptr && g_button->contains(rendering_engine::window_to_pixels(math::vec2{x, y}));
    }
} // namespace

static void on_engine_start(const core::engine_start& event)
{
    auto& eng = runtime::current_engine();
    auto& renderer = *eng.renderer;

    g_button = std::make_unique<rendering_engine::pane>(&renderer.get_ui_material(), button_size);
    g_button->set_anchor(rendering_engine::ui_anchor::bottom_right);
    g_button->set_pivot(rendering_engine::ui_anchor::bottom_right);
    g_button->set_position(-button_margin);
    g_button->set_color(idle_color);
    renderer.register_ui_renderable(g_button.get());

    const char* font_path = std::getenv("ALPHAENGINE_UI_FONT");
    if (font_path == nullptr || *font_path == '\0')
    {
        return;
    }
    try
    {
        g_caption = std::make_unique<rendering_engine::label>(
            eng.assets->load_font(font_path, 18.0f), &renderer.get_ui_material(), "Esc");
        // Centred on the button: same anchor, pivot at the text's centre.
        g_caption->set_anchor(rendering_engine::ui_anchor::bottom_right);
        g_caption->set_pivot(rendering_engine::ui_anchor::center);
        g_caption->set_position(-(button_margin + button_size * 0.5f));
        renderer.register_ui_renderable(g_caption.get());
    }
    catch (const std::exception& e)
    {
        LOG_WRN("ui_demo_module: no caption for the quit button: %s", e.what());
    }
}

static void on_engine_stop(const core::engine_stop& event)
{
    auto& renderer = *runtime::current_engine().renderer;
    if (g_caption != nullptr)
    {
        renderer.unregister_ui_renderable(g_caption.get());
        g_caption.reset();
    }
    if (g_button != nullptr)
    {
        renderer.unregister_ui_renderable(g_button.get());
        g_button.reset();
    }
    g_hovered = false;
}

static void on_mouse_move(const core::mouse_move& event)
{
    const bool hovered = is_over_button(event.m_x, event.m_y);
    if (g_button != nullptr && hovered != g_hovered)
    {
        g_hovered = hovered;
        g_button->set_color(hovered ? hover_color : idle_color);
    }
}

static void on_mouse_key_down(const core::mouse_key_down& event)
{
    if (event.m_key_code == core::mouse_key_code::left && is_over_button(event.m_x, event.m_y))
    {
        runtime::current_engine().events->emit<core::quit_requested>();
    }
}

GAME_MODULE()
{
    LOG_INF("Registering external module: ui_demo_module");
    struct game_module_info info;
    info.on_engine_start = on_engine_start;
    info.on_engine_stop = on_engine_stop;
    info.on_mouse_move = on_mouse_move;
    info.on_mouse_key_down = on_mouse_key_down;
    register_game_module(info);
    return true;
}
