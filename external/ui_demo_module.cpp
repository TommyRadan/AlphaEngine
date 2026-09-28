// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include "api/game_module.hpp"

#include <core/event_engine.hpp>
#include <core/log.hpp>
#include <core/math/vec2.hpp>
#include <core/subscription.hpp>
#include <platform/window.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/renderables/premade_2d/label.hpp>
#include <rendering_engine/renderables/premade_2d/pane.hpp>
#include <rendering_engine/renderer.hpp>
#include <rendering_engine/resources/asset_cache.hpp>
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
    constexpr assets::color idle_color{20, 20, 24, 160};
    constexpr assets::color hover_color{200, 60, 50, 220};

    /**
     * The quit button: a pane that recolours under the cursor and quits the
     * engine when clicked, with an optional "Esc" caption.
     *
     * The pane and its caption are created once, in on_start, and released
     * in on_destroy; the mouse input they react to is only listened for
     * while the behaviour is enabled, subscribed in on_enable and dropped in
     * on_disable, the same way fly_camera holds its input tokens.
     */
    struct ui_quit_button final : runtime::behavior
    {
        void on_enable() override
        {
            core::event_bus& events = *runtime::current_engine().events;
            m_mouse_move =
                events.subscribe<core::mouse_move>([this](const core::mouse_move& event) { update_hover(event); });
            m_mouse_key_down =
                events.subscribe<core::mouse_key_down>([this](const core::mouse_key_down& event) { try_quit(event); });
        }

        void on_disable() override
        {
            m_mouse_move.reset();
            m_mouse_key_down.reset();
            m_hovered = false;
        }

        void on_start() override
        {
            auto& eng = runtime::current_engine();
            auto& renderer = *eng.renderer;

            m_button = std::make_unique<rendering_engine::pane>(*eng.gpu, &renderer.get_ui_material(), button_size);
            m_button->set_anchor(rendering_engine::ui_anchor::bottom_right);
            m_button->set_pivot(rendering_engine::ui_anchor::bottom_right);
            m_button->set_position(-button_margin);
            m_button->set_color(idle_color);
            renderer.register_ui_renderable(m_button.get());

            const char* font_path = std::getenv("ALPHAENGINE_UI_FONT");
            if (font_path == nullptr || *font_path == '\0')
            {
                return;
            }
            try
            {
                m_caption = std::make_unique<rendering_engine::label>(
                    *eng.gpu, eng.assets->load_font(font_path, 18.0f), &renderer.get_ui_material(), "Esc");
                // Centred on the button: same anchor, pivot at the text's centre.
                m_caption->set_anchor(rendering_engine::ui_anchor::bottom_right);
                m_caption->set_pivot(rendering_engine::ui_anchor::center);
                m_caption->set_position(-(button_margin + button_size * 0.5f));
                renderer.register_ui_renderable(m_caption.get());
            }
            catch (const std::exception& e)
            {
                LOG_WRN("ui_demo_module: no caption for the quit button: %s", e.what());
            }
        }

        void on_destroy() override
        {
            auto& renderer = *runtime::current_engine().renderer;
            if (m_caption)
            {
                renderer.unregister_ui_renderable(m_caption.get());
                m_caption.reset();
            }
            if (m_button)
            {
                renderer.unregister_ui_renderable(m_button.get());
                m_button.reset();
            }
        }

    private:
        bool is_over_button(float x, float y) const
        {
            if (m_button == nullptr)
            {
                return false;
            }
            const platform::window& window = *runtime::current_engine().window;
            const platform::window_extent logical = window.size();
            const platform::window_extent pixels = window.pixel_size();
            const math::vec2 window_size{static_cast<float>(logical.width), static_cast<float>(logical.height)};
            const math::vec2 pixel_size{static_cast<float>(pixels.width), static_cast<float>(pixels.height)};
            return m_button->contains(rendering_engine::drawable_rect(pixel_size),
                                      rendering_engine::window_to_pixels(math::vec2{x, y}, window_size, pixel_size));
        }

        void update_hover(const core::mouse_move& event)
        {
            const bool hovered = is_over_button(event.m_x, event.m_y);
            if (m_button != nullptr && hovered != m_hovered)
            {
                m_hovered = hovered;
                m_button->set_color(hovered ? hover_color : idle_color);
            }
        }

        void try_quit(const core::mouse_key_down& event)
        {
            if (event.m_key_code == core::mouse_key_code::left && is_over_button(event.m_x, event.m_y))
            {
                runtime::current_engine().events->emit<core::quit_requested>();
            }
        }

        std::unique_ptr<rendering_engine::pane> m_button;
        std::unique_ptr<rendering_engine::label> m_caption;
        bool m_hovered{false};

        // Input listeners, held only while the button is enabled.
        core::subscription m_mouse_move;
        core::subscription m_mouse_key_down;
    };
} // namespace

REFLECT_TYPES()
{
    registry.register_behavior<ui_quit_button>("ui_quit_button");
}

GAME_MODULE()
{
    runtime::node& button = scene.create_node("ui_quit_button");
    runtime::add_behavior<ui_quit_button>(button);
}
