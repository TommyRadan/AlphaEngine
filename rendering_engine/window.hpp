// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file window.hpp
 * @brief SDL-backed OS window owner; source of input events.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

struct SDL_Window;
struct SDL_Gamepad;

namespace rendering_engine
{
    /** @brief Custom deleter that destroys an @c SDL_Window via @c SDL_DestroyWindow. */
    struct sdl_window_deleter
    {
        void operator()(SDL_Window* w) const noexcept;
    };

    /** @brief Custom deleter that closes an @c SDL_Gamepad via @c SDL_CloseGamepad. */
    struct sdl_gamepad_deleter
    {
        void operator()(SDL_Gamepad* gamepad) const noexcept;
    };

    /** @brief RAII-owning handle to an @c SDL_Window. */
    using sdl_window_handle = std::unique_ptr<SDL_Window, sdl_window_deleter>;

    /** @brief RAII-owning handle to an opened @c SDL_Gamepad. */
    using sdl_gamepad_handle = std::unique_ptr<SDL_Gamepad, sdl_gamepad_deleter>;

    /** @brief The open gamepads, keyed by their platform joystick instance id. */
    using gamepad_map = std::unordered_map<std::uint32_t, sdl_gamepad_handle>;

    /** @brief Severity of a message box shown through @ref window::show_message. */
    enum class message_severity
    {
        information,
        warning,
        error,
    };

    /** @brief A window size, in either logical points or pixels depending on the accessor. */
    struct window_extent
    {
        std::uint32_t width{0};
        std::uint32_t height{0};
    };

    /**
     * @brief Owns the application window and pumps OS input.
     *
     * Owned by @ref runtime::engine. The window is held as a
     * @c std::unique_ptr with an SDL-specific deleter, so its lifetime
     * is strictly tied to this instance — @ref init creates it and
     * @ref quit (or destruction) releases it. Gamepads are
     * opened as they connect and closed as they disconnect (or at
     * @ref quit). All methods must be invoked from the main thread.
     */
    struct window
    {
        window();

        /**
         * @brief Initializes SDL video (and the gamepad subsystem) and
         *        creates the Vulkan-capable window using the dimensions and
         *        flags read from the engine-wide @ref core::settings object.
         *        A zero width or height ("match the display", the release
         *        default) is resolved against the primary display once video
         *        is up and written back to the settings.
         *
         * The window is resizable and requests a high-pixel-density
         * drawable, so @ref pixel_size may exceed @ref size on scaled
         * displays. A failed gamepad init only logs a warning.
         * @throws std::runtime_error if SDL video init or window creation fails.
         */
        void init();

        /** @brief Closes open gamepads, destroys the window and shuts down SDL video. */
        void quit();

        /**
         * @brief Pumps the SDL event queue and translates OS input into
         *        engine events broadcast through @ref core::event_bus.
         *
         * Keyboard, mouse (buttons, motion, wheel), text input, window
         * (resize, focus, minimize, close) and gamepad events are
         * forwarded; input the debug overlay is capturing is skipped.
         * Buttons and keys the engine has no name for are dropped rather
         * than emitted with a guessed code.
         *
         * Variable-rate: call once per rendered frame. The fixed-step
         * @ref core::frame update is driven separately by
         * @ref runtime::engine::tick.
         */
        void tick();

        /**
         * @brief Displays a modal message box parented to the window.
         * @param title    Title of the message box.
         * @param message  Body text of the message box.
         * @param severity Icon / styling of the box; defaults to an error box.
         */
        void show_message(const std::string& title,
                          const std::string& message,
                          message_severity severity = message_severity::error);

        /** @brief Shows the OS cursor. Independent of relative mouse mode. */
        void show_cursor();

        /** @brief Hides the OS cursor. Independent of relative mouse mode. */
        void hide_cursor();

        /**
         * @brief Enables or disables relative mouse mode: the cursor is
         *        locked in place and @ref core::mouse_move reports only
         *        deltas (as for a first-person camera). Visibility of the
         *        cursor is controlled separately by @ref show_cursor /
         *        @ref hide_cursor.
         */
        void set_relative_mouse_mode(bool enabled);

        /** @brief Whether relative mouse mode is currently enabled. */
        bool relative_mouse_mode() const noexcept;

        /**
         * @brief Starts or stops OS text input. While enabled the window
         *        emits @ref core::text_input for committed text (and the
         *        platform may show an on-screen keyboard / IME); key events
         *        keep flowing either way. Off by default.
         */
        void set_text_input(bool enabled);

        /** @brief Whether text input is currently enabled. */
        bool text_input_active() const noexcept;

        /**
         * @brief Current window size in logical points — the coordinate
         *        space of the mouse events. Zero before @ref init.
         */
        window_extent size() const noexcept;

        /**
         * @brief Current drawable size in pixels — what the swapchain and
         *        render targets must be sized to. Differs from @ref size
         *        by the display scale on high-density displays. Zero
         *        before @ref init.
         */
        window_extent pixel_size() const noexcept;

        /**
         * @brief Whether the window is currently minimized. A minimized
         *        window has no drawable, so the main loop skips rendering
         *        until it is restored.
         */
        bool is_minimized() const noexcept;

        /**
         * @brief Returns the underlying @c SDL_Window pointer.
         *
         * Exposed so the active GPU backend can plumb the window into
         * its own surface-creation path (e.g. @c SDL_Vulkan_CreateSurface).
         * Ownership stays with the @ref window — callers must not
         * destroy or hold the pointer beyond @ref quit.
         */
        SDL_Window* sdl_window() const noexcept;

    private:
        sdl_window_handle m_window;
        gamepad_map m_gamepads;
        bool m_gamepad_subsystem{false};
        bool m_minimized{false};
    };
} // namespace rendering_engine
