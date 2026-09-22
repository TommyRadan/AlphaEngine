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

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

#include <core/event_engine.hpp>
#include <core/log.hpp>
#include <core/settings.hpp>
#include <core/time.hpp>
#include <rendering_engine/debug_ui/imgui_layer.hpp>
#include <rendering_engine/sdl_input.hpp>
#include <rendering_engine/util/color.hpp>
#include <rendering_engine/window.hpp>
#include <runtime/engine.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

namespace rendering_engine
{
    namespace
    {
        SDL_MessageBoxFlags to_message_box_flags(message_severity severity) noexcept
        {
            switch (severity)
            {
            case message_severity::information:
                return SDL_MESSAGEBOX_INFORMATION;
            case message_severity::warning:
                return SDL_MESSAGEBOX_WARNING;
            case message_severity::error:
                break;
            }
            return SDL_MESSAGEBOX_ERROR;
        }

        // SDL reports sizes as signed ints; the engine's extents are unsigned.
        std::uint32_t to_extent(int value) noexcept
        {
            return static_cast<std::uint32_t>(std::max(value, 0));
        }

        // The window-family and gamepad-family dispatchers live here rather
        // than as members so the header need not name SDL_Event; they get the
        // window state they touch handed in.
        void dispatch_window_event(const SDL_Event& event, core::event_bus& bus, const window& self, bool& minimized);
        void dispatch_gamepad_event(const SDL_Event& event, core::event_bus& bus, gamepad_map& gamepads);
    } // namespace

    void sdl_window_deleter::operator()(SDL_Window* w) const noexcept
    {
        if (w != nullptr)
        {
            SDL_DestroyWindow(w);
        }
    }

    void sdl_gl_context_deleter::operator()(void* ctx) const noexcept
    {
        if (ctx != nullptr)
        {
            SDL_GL_DestroyContext(static_cast<SDL_GLContext>(ctx));
        }
    }

    void sdl_gamepad_deleter::operator()(SDL_Gamepad* gamepad) const noexcept
    {
        if (gamepad != nullptr)
        {
            SDL_CloseGamepad(gamepad);
        }
    }

    window::window() = default;

    void window::init()
    {
        LOG_INF("Init rendering_engine::window");

        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            LOG_FTL("Could not initialize video system");
            LOG_FTL("SDL_Error: %s", SDL_GetError());
            throw std::runtime_error{"Could not initialize video system"};
        }

        // Gamepads are optional: a platform without joystick support still
        // gets a window and keyboard / mouse input, it just never sees a
        // gamepad_* event.
        m_gamepad_subsystem = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        if (!m_gamepad_subsystem)
        {
            LOG_WRN("Could not initialize gamepad subsystem: %s", SDL_GetError());
        }

        ::settings& s{*runtime::current_engine().settings};
        m_is_vulkan = s.graphics.backend == graphics_backend::vulkan;
        SDL_WindowFlags window_flags{m_is_vulkan ? SDL_WINDOW_VULKAN : SDL_WINDOW_OPENGL};
        // Resizable so the user (and the window manager) can change the size,
        // reported through window_resized; high-pixel-density so the drawable
        // matches the display's native pixel grid instead of a scaled logical
        // size — pixel_size() is what the swapchain follows.
        window_flags |= SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        auto type{s.window.type};

        const char* type_name = "windowed";
        bool fullscreen = false;
        if (type == win_type::win_type_borderless)
        {
            window_flags |= SDL_WINDOW_BORDERLESS;
            type_name = "borderless";
        }

        if (type == win_type::win_type_fullscreen)
        {
            window_flags |= SDL_WINDOW_FULLSCREEN;
            type_name = "fullscreen";
            fullscreen = true;
        }

        LOG_INF("Creating window: name='%s' size=%ux%u mode=%s double_buffered=%s",
                s.window.name.c_str(),
                s.window.width,
                s.window.height,
                type_name,
                s.window.double_buffered ? "true" : "false");

        if (!m_is_vulkan)
        {
            SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
            SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
            SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
            SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
            SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, s.window.double_buffered);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
        }

        m_window.reset(SDL_CreateWindow(s.window.name.c_str(), s.window.width, s.window.height, window_flags));

        if (m_window == nullptr)
        {
            LOG_FTL("Cannot create window");
            LOG_FTL("SDL Error: %s", SDL_GetError());
            throw std::runtime_error{SDL_GetError()};
        }

        const window_extent pixels = pixel_size();
        LOG_INF("Window drawable: %ux%u pixels", pixels.width, pixels.height);

        if (fullscreen)
        {
            hide_cursor();
            set_relative_mouse_mode(true);
        }

        if (m_is_vulkan)
        {
            // Vulkan owns presentation through vkQueuePresentKHR; the
            // window stays an SDL_WINDOW_VULKAN container only.
            LOG_INF("SDL window created (Vulkan presentation)");
        }
        else
        {
            m_gl_context.reset(SDL_GL_CreateContext(m_window.get()));
            if (m_gl_context == nullptr)
            {
                LOG_FTL("Could not create SDL GL context: %s", SDL_GetError());
                throw std::runtime_error{SDL_GetError()};
            }
            LOG_INF("SDL window and GL context created successfully");
            const int swap_interval = s.window.vsync ? 1 : 0;
            if (!SDL_GL_SetSwapInterval(swap_interval))
            {
                LOG_WRN("Could not set swap interval to %d: %s", swap_interval, SDL_GetError());
            }
        }
    }

    void window::quit()
    {
        // Close the gamepads before the subsystem that owns them goes.
        m_gamepads.clear();
        m_gl_context.reset();
        m_window.reset();

        if (m_gamepad_subsystem)
        {
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
            m_gamepad_subsystem = false;
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO);

        LOG_INF("Quit rendering_engine::window");
    }

    void window::swap_buffers()
    {
        // Vulkan presents through vkQueuePresentKHR inside
        // gpu::device::submit; nothing to do here on that path.
        if (!m_is_vulkan)
        {
            SDL_GL_SwapWindow(m_window.get());
        }
    }

    SDL_Window* window::sdl_window() const noexcept
    {
        return m_window.get();
    }

    void* window::gl_context() const noexcept
    {
        return m_gl_context.get();
    }

    void window::show_message(const std::string& title, const std::string& message, message_severity severity)
    {
        SDL_ShowSimpleMessageBox(to_message_box_flags(severity), title.c_str(), message.c_str(), m_window.get());
    }

    void window::show_cursor()
    {
        SDL_ShowCursor();
    }

    void window::hide_cursor()
    {
        SDL_HideCursor();
    }

    void window::set_relative_mouse_mode(bool enabled)
    {
        if (m_window == nullptr)
        {
            return;
        }
        if (!SDL_SetWindowRelativeMouseMode(m_window.get(), enabled))
        {
            LOG_WRN("Could not %s relative mouse mode: %s", enabled ? "enable" : "disable", SDL_GetError());
        }
    }

    bool window::relative_mouse_mode() const noexcept
    {
        return m_window != nullptr && SDL_GetWindowRelativeMouseMode(m_window.get());
    }

    void window::set_text_input(bool enabled)
    {
        if (m_window == nullptr)
        {
            return;
        }
        const bool ok = enabled ? SDL_StartTextInput(m_window.get()) : SDL_StopTextInput(m_window.get());
        if (!ok)
        {
            LOG_WRN("Could not %s text input: %s", enabled ? "start" : "stop", SDL_GetError());
        }
    }

    bool window::text_input_active() const noexcept
    {
        return m_window != nullptr && SDL_TextInputActive(m_window.get());
    }

    window_extent window::size() const noexcept
    {
        window_extent extent{};
        int width = 0;
        int height = 0;
        if (m_window != nullptr && SDL_GetWindowSize(m_window.get(), &width, &height))
        {
            extent.width = to_extent(width);
            extent.height = to_extent(height);
        }
        return extent;
    }

    window_extent window::pixel_size() const noexcept
    {
        window_extent extent{};
        int width = 0;
        int height = 0;
        if (m_window != nullptr && SDL_GetWindowSizeInPixels(m_window.get(), &width, &height))
        {
            extent.width = to_extent(width);
            extent.height = to_extent(height);
        }
        return extent;
    }

    bool window::is_minimized() const noexcept
    {
        return m_minimized;
    }

    void window::tick()
    {
        SDL_Event event{};

        auto& eng = runtime::current_engine();
        auto& bus = *eng.events;

        SDL_PumpEvents();
        while (SDL_PollEvent(&event))
        {
            // Let the debug UI see every event first. When a debug panel
            // has focus it captures the matching input class so the same
            // click / keystroke does not also drive the camera or game
            // modules. Both calls are no-ops in release builds.
            debug_ui::process_event(&event);
            const bool ui_wants_keyboard = debug_ui::wants_keyboard();
            const bool ui_wants_mouse = debug_ui::wants_mouse();

            switch (event.type)
            {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            {
                if (ui_wants_keyboard)
                {
                    break;
                }
                // Keys the engine has no name for arrive as key_code::unknown
                // rather than as a raw keycode cast into the enum.
                const core::key_code code = sdl_input::to_key_code(event.key.key);
                if (event.type == SDL_EVENT_KEY_DOWN)
                {
                    core::key_down key_down{};
                    key_down.m_key_code = code;
                    bus.emit<core::key_down>(key_down);
                }
                else
                {
                    core::key_up key_up{};
                    key_up.m_key_code = code;
                    bus.emit<core::key_up>(key_up);
                }
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                if (ui_wants_mouse)
                {
                    break;
                }
                // A button without an engine name is dropped outright: there
                // is no honest code to emit for it, and a guessed one (a
                // spurious left-button release, say) would cancel whatever
                // the real left button is doing.
                const std::optional<core::mouse_key_code> code = sdl_input::to_mouse_key_code(event.button.button);
                if (!code.has_value())
                {
                    break;
                }
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                {
                    core::mouse_key_down mouse_key_down{};
                    mouse_key_down.m_key_code = *code;
                    mouse_key_down.m_x = event.button.x;
                    mouse_key_down.m_y = event.button.y;
                    bus.emit<core::mouse_key_down>(mouse_key_down);
                }
                else
                {
                    core::mouse_key_up mouse_key_up{};
                    mouse_key_up.m_key_code = *code;
                    mouse_key_up.m_x = event.button.x;
                    mouse_key_up.m_y = event.button.y;
                    bus.emit<core::mouse_key_up>(mouse_key_up);
                }
                break;
            }

            case SDL_EVENT_MOUSE_MOTION:
            {
                if (ui_wants_mouse)
                {
                    break;
                }
                // SDL3 reports motion as floats; keep the fraction so slow
                // drags on high-polling-rate or scaled mice are not
                // truncated to zero.
                core::mouse_move mouse_move{};
                mouse_move.m_delta_x = event.motion.xrel;
                mouse_move.m_delta_y = event.motion.yrel;
                mouse_move.m_x = event.motion.x;
                mouse_move.m_y = event.motion.y;
                bus.emit<core::mouse_move>(mouse_move);
                break;
            }

            case SDL_EVENT_MOUSE_WHEEL:
            {
                if (ui_wants_mouse)
                {
                    break;
                }
                core::mouse_wheel mouse_wheel{};
                mouse_wheel.m_delta_x = event.wheel.x;
                mouse_wheel.m_delta_y = event.wheel.y;
                mouse_wheel.m_x = event.wheel.mouse_x;
                mouse_wheel.m_y = event.wheel.mouse_y;
                bus.emit<core::mouse_wheel>(mouse_wheel);
                break;
            }

            case SDL_EVENT_TEXT_INPUT:
            {
                if (ui_wants_keyboard || event.text.text == nullptr)
                {
                    break;
                }
                core::text_input text_input{};
                text_input.m_text = event.text.text;
                bus.emit<core::text_input>(std::move(text_input));
                break;
            }

            case SDL_EVENT_GAMEPAD_ADDED:
            case SDL_EVENT_GAMEPAD_REMOVED:
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                dispatch_gamepad_event(event, bus, m_gamepads);
                break;

            case SDL_EVENT_QUIT:
                bus.emit<core::quit_requested>();
                break;

            default:
                if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST)
                {
                    dispatch_window_event(event, bus, *this, m_minimized);
                }
                break;
            }
        }
    }

    namespace
    {
        void dispatch_window_event(const SDL_Event& event, core::event_bus& bus, const window& self, bool& minimized)
        {
            switch (event.type)
            {
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            {
                // The drawable changed: a user resize, a DPI-scale change or a
                // move onto a display with another scale. SDL_EVENT_WINDOW_RESIZED
                // only covers the logical size, so this is the event the
                // swapchain follows; the logical size is read alongside it.
                const window_extent logical = self.size();
                core::window_resized resized{};
                resized.m_width = logical.width;
                resized.m_height = logical.height;
                resized.m_pixel_width = to_extent(event.window.data1);
                resized.m_pixel_height = to_extent(event.window.data2);
                bus.emit<core::window_resized>(resized);
                break;
            }

            case SDL_EVENT_WINDOW_MINIMIZED:
                if (!minimized)
                {
                    minimized = true;
                    core::window_minimized changed{};
                    changed.m_minimized = true;
                    bus.emit<core::window_minimized>(changed);
                }
                break;

            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
                // Maximizing a minimized window skips the restored event.
                if (minimized)
                {
                    minimized = false;
                    core::window_minimized changed{};
                    changed.m_minimized = false;
                    bus.emit<core::window_minimized>(changed);
                }
                break;

            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            {
                core::window_focus focus{};
                focus.m_gained = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
                bus.emit<core::window_focus>(focus);
                break;
            }

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                bus.emit<core::window_close_requested>();
                break;

            default:
                break;
            }
        }

        void dispatch_gamepad_event(const SDL_Event& event, core::event_bus& bus, gamepad_map& gamepads)
        {
            switch (event.type)
            {
            case SDL_EVENT_GAMEPAD_ADDED:
            {
                // SDL only delivers button / axis events for gamepads that were
                // opened, so open each one as it appears (including the ones
                // already plugged in at init, which SDL announces the same way).
                const SDL_JoystickID id = event.gdevice.which;
                sdl_gamepad_handle gamepad{SDL_OpenGamepad(id)};
                if (gamepad == nullptr)
                {
                    LOG_WRN("Could not open gamepad %u: %s", id, SDL_GetError());
                    break;
                }
                const char* name = SDL_GetGamepadName(gamepad.get());
                LOG_INF("Gamepad connected: id=%u name='%s'", id, name != nullptr ? name : "unknown");
                gamepads[id] = std::move(gamepad);
                core::gamepad_connected connected{};
                connected.m_id = id;
                bus.emit<core::gamepad_connected>(connected);
                break;
            }

            case SDL_EVENT_GAMEPAD_REMOVED:
            {
                const SDL_JoystickID id = event.gdevice.which;
                // Only gamepads that were opened (and announced) are reported
                // as disconnected.
                if (gamepads.erase(id) == 0)
                {
                    break;
                }
                LOG_INF("Gamepad disconnected: id=%u", id);
                core::gamepad_disconnected disconnected{};
                disconnected.m_id = id;
                bus.emit<core::gamepad_disconnected>(disconnected);
                break;
            }

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            {
                core::gamepad_button button{};
                button.m_id = event.gbutton.which;
                button.m_button =
                    sdl_input::to_gamepad_button_code(static_cast<SDL_GamepadButton>(event.gbutton.button));
                button.m_pressed = event.gbutton.down;
                bus.emit<core::gamepad_button>(button);
                break;
            }

            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            {
                core::gamepad_axis axis{};
                axis.m_id = event.gaxis.which;
                axis.m_axis = sdl_input::to_gamepad_axis_code(static_cast<SDL_GamepadAxis>(event.gaxis.axis));
                axis.m_value = sdl_input::normalize_axis(event.gaxis.value);
                bus.emit<core::gamepad_axis>(axis);
                break;
            }

            default:
                break;
            }
        }
    } // namespace
} // namespace rendering_engine
