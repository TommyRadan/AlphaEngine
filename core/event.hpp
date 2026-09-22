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

/**
 * @file event.hpp
 * @brief Built-in event value types broadcast through the event bus.
 *
 * Every event is an ordinary value type — there is no shared base class.
 * The @ref core::event_bus is keyed on @c std::type_index, so game
 * modules can define their own event structs and dispatch them through
 * the same bus without modifying this header.
 */

#pragma once

#include <cstdint>
#include <string>

namespace rendering_engine::gpu
{
    struct render_pass_encoder;
}

namespace core
{
    /**
     * @brief Keyboard key identifiers.
     *
     * Values are engine-owned and dense: the window layer translates the
     * platform keycode into this enum and reports any key it has no name
     * for as @ref key_code::unknown rather than casting it through. Keys
     * are identified by their keycode (the label on the key under the
     * active layout), not by physical position. Left- and right-hand
     * modifiers are distinct so a caller can tell them apart or treat
     * them alike.
     */
    enum class key_code
    {
        unknown = 0,

        // Letters
        a,
        b,
        c,
        d,
        e,
        f,
        g,
        h,
        i,
        j,
        k,
        l,
        m,
        n,
        o,
        p,
        q,
        r,
        s,
        t,
        u,
        v,
        w,
        x,
        y,
        z,

        // Top-row digits
        num_0,
        num_1,
        num_2,
        num_3,
        num_4,
        num_5,
        num_6,
        num_7,
        num_8,
        num_9,

        // Function keys
        f1,
        f2,
        f3,
        f4,
        f5,
        f6,
        f7,
        f8,
        f9,
        f10,
        f11,
        f12,

        // Editing and whitespace
        enter,
        escape,
        backspace,
        tab,
        space,
        insert,
        del,
        home,
        end,
        page_up,
        page_down,

        // Arrows
        left,
        right,
        up,
        down,

        // Modifiers
        left_shift,
        right_shift,
        left_ctrl,
        right_ctrl,
        left_alt,
        right_alt,
        left_gui,
        right_gui,

        // Locks and system keys
        caps_lock,
        num_lock,
        scroll_lock,
        print_screen,
        pause,
        menu,

        // Punctuation (US-layout labels)
        minus,
        equals,
        left_bracket,
        right_bracket,
        backslash,
        semicolon,
        apostrophe,
        grave,
        comma,
        period,
        slash,

        // Keypad
        keypad_0,
        keypad_1,
        keypad_2,
        keypad_3,
        keypad_4,
        keypad_5,
        keypad_6,
        keypad_7,
        keypad_8,
        keypad_9,
        keypad_divide,
        keypad_multiply,
        keypad_minus,
        keypad_plus,
        keypad_enter,
        keypad_period,
        keypad_equals,

        count /**< Number of named keys; not a key. */
    };

    /** @brief Mouse button identifiers. */
    enum class mouse_key_code
    {
        left,
        right,
        middle,
        x1, /**< First extra (thumb / "back") button. */
        x2, /**< Second extra (thumb / "forward") button. */
    };

    /**
     * @brief Gamepad button identifiers in the SDL gamepad layout. Face
     *        buttons are positional: @c south is the Xbox A / PlayStation
     *        cross button.
     */
    enum class gamepad_button_code
    {
        unknown = 0,
        south,
        east,
        west,
        north,
        back,
        guide,
        start,
        left_stick,
        right_stick,
        left_shoulder,
        right_shoulder,
        dpad_up,
        dpad_down,
        dpad_left,
        dpad_right,
        misc1,
        right_paddle1,
        left_paddle1,
        right_paddle2,
        left_paddle2,
        touchpad,
        misc2,
        misc3,
        misc4,
        misc5,
        misc6,
    };

    /** @brief Gamepad axis identifiers. */
    enum class gamepad_axis_code
    {
        unknown = 0,
        left_x,
        left_y,
        right_x,
        right_y,
        left_trigger,
        right_trigger,
    };

    /** @brief Broadcast once after all subsystems have been initialized. */
    struct engine_start
    {
    };

    /** @brief Broadcast once as the main loop is tearing down. */
    struct engine_stop
    {
    };

    /** @brief Signals the user requested application termination. */
    struct quit_requested
    {
    };

    /**
     * @brief Per-frame tick event.
     *
     * Broadcast once per iteration of the main loop and carries the
     * time elapsed since the previous frame.
     */
    struct frame
    {
        /** @brief Time since the previous frame, in milliseconds. */
        float m_delta_time;
    };

    /**
     * @brief Per-rendered-frame update event (variable rate).
     *
     * Broadcast once per render frame, carrying the real time since the
     * previous render frame. Use this for visual / input-driven animation that
     * must stay smooth at the render rate — a camera that moves with the player,
     * spinning props — so motion does not judder when the render rate runs far
     * ahead of the fixed-step rate. Use @ref frame instead for deterministic,
     * frame-rate-independent simulation.
     */
    struct render_update
    {
        /** @brief Time since the previous render frame, in milliseconds. */
        float m_delta_time;
    };

    /**
     * @brief Broadcast while the 3D scene pass is active; renderables should
     *        record draw calls against the carried pass encoder.
     */
    struct render_scene
    {
        rendering_engine::gpu::render_pass_encoder* encoder{nullptr};
    };

    /**
     * @brief Broadcast while the 2D overlay/UI pass is active. Renderables
     *        record draws against the carried pass encoder.
     */
    struct render_ui
    {
        rendering_engine::gpu::render_pass_encoder* encoder{nullptr};
    };

    /**
     * @brief Broadcast while the debug-overlay pass is active. Documented
     *        escape hatch for debug-line, gizmo, frustum and bounds
     *        visualisations whose draw cadence does not match the
     *        debug-renderable registry walk.
     *
     * The debug pass is only appended to the pass list in debug builds,
     * so subscribers will not see this event in release configurations.
     */
    struct render_debug
    {
        rendering_engine::gpu::render_pass_encoder* encoder{nullptr};
    };

    /**
     * @brief Key release. @ref m_key_code is the released key, or
     *        @ref key_code::unknown for a key the engine does not name.
     */
    struct key_up
    {
        key_code m_key_code;
    };

    /**
     * @brief Key press (repeats while held). @ref m_key_code is the pressed
     *        key, or @ref key_code::unknown for a key the engine does not name.
     */
    struct key_down
    {
        key_code m_key_code;
    };

    /**
     * @brief Mouse button release.
     *
     * @ref m_x and @ref m_y are the cursor position at the time of the
     * release, in window coordinates (logical points, origin top-left).
     */
    struct mouse_key_up
    {
        mouse_key_code m_key_code;
        float m_x;
        float m_y;
    };

    /**
     * @brief Mouse button press.
     *
     * @ref m_x and @ref m_y are the cursor position at the time of the
     * press, in window coordinates (logical points, origin top-left).
     */
    struct mouse_key_down
    {
        mouse_key_code m_key_code;
        float m_x;
        float m_y;
    };

    /**
     * @brief Mouse motion event.
     *
     * Carries both the relative motion since the previous report and the
     * absolute cursor position. Deltas keep their sub-pixel fraction so
     * slow drags on high-resolution or scaled mice are not truncated away;
     * in relative mouse mode the position stays where the cursor was
     * locked. All values are in window coordinates (logical points).
     */
    struct mouse_move
    {
        float m_delta_x; /**< Horizontal motion since the previous report. */
        float m_delta_y; /**< Vertical motion since the previous report. */
        float m_x;       /**< Cursor x position, relative to the window. */
        float m_y;       /**< Cursor y position, relative to the window. */
    };

    /**
     * @brief Mouse wheel event.
     *
     * @ref m_delta_y is positive when scrolled away from the user and
     * negative toward the user; @ref m_delta_x is positive to the right.
     * Precision wheels and trackpads report fractional amounts. The
     * cursor position at the time of the scroll is carried as well so UI
     * can route the scroll to the widget under the pointer.
     */
    struct mouse_wheel
    {
        float m_delta_x; /**< Horizontal scroll amount. */
        float m_delta_y; /**< Vertical scroll amount. */
        float m_x;       /**< Cursor x position, relative to the window. */
        float m_y;       /**< Cursor y position, relative to the window. */
    };

    /**
     * @brief Text input event: committed text from the keyboard or IME,
     *        UTF-8 encoded. Only delivered while text input is enabled via
     *        @c rendering_engine::window::set_text_input.
     */
    struct text_input
    {
        std::string m_text;
    };

    /**
     * @brief The window's drawable changed size.
     *
     * Emitted for a user resize, a DPI-scale change, or a move onto a
     * display with a different scale. @ref m_width / @ref m_height are the
     * logical window size (the coordinate space of the mouse events);
     * @ref m_pixel_width / @ref m_pixel_height are the drawable size in
     * pixels, which is what the swapchain and render targets use. On a
     * high-density display the two differ by the display scale.
     */
    struct window_resized
    {
        std::uint32_t m_width;
        std::uint32_t m_height;
        std::uint32_t m_pixel_width;
        std::uint32_t m_pixel_height;
    };

    /** @brief Keyboard focus moved onto (@c true) or away from (@c false) the window. */
    struct window_focus
    {
        bool m_gained;
    };

    /** @brief The window was minimized (@c true) or restored from minimized (@c false). */
    struct window_minimized
    {
        bool m_minimized;
    };

    /**
     * @brief The window manager asked for the window to be closed (close
     *        button, Alt+F4, ...). @ref quit_requested follows when it is
     *        the last window; this event lets a listener react to the
     *        request itself.
     */
    struct window_close_requested
    {
    };

    /** @brief A gamepad was connected. @ref m_id identifies it in later events. */
    struct gamepad_connected
    {
        std::uint32_t m_id; /**< Platform joystick instance id, unique for the session. */
    };

    /** @brief The gamepad with id @ref m_id was disconnected. */
    struct gamepad_disconnected
    {
        std::uint32_t m_id;
    };

    /** @brief A gamepad button was pressed or released. */
    struct gamepad_button
    {
        std::uint32_t m_id;
        gamepad_button_code m_button;
        bool m_pressed;
    };

    /**
     * @brief A gamepad axis moved. Sticks report @ref m_value in [-1, 1]
     *        (y grows downward), triggers in [0, 1].
     */
    struct gamepad_axis
    {
        std::uint32_t m_id;
        gamepad_axis_code m_axis;
        float m_value;
    };
} // namespace core
