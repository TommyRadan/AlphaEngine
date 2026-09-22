// Unit tests for rendering_engine::sdl_input: the translation of SDL keycodes,
// mouse-button indices, gamepad buttons and gamepad axes into the engine's
// core enums, and the axis normalisation. Everything here is a pure function
// over SDL's header constants — no SDL subsystem is initialised and no window
// exists, so the suite runs headless.

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>

#include <core/event.hpp>
#include <rendering_engine/sdl_input.hpp>

namespace
{
    using core::gamepad_axis_code;
    using core::gamepad_button_code;
    using core::key_code;
    using core::mouse_key_code;
    using namespace rendering_engine::sdl_input;
} // namespace

// -- Keyboard -----------------------------------------------------------------

TEST(sdl_input, letters_digits_and_space_map_to_their_keys)
{
    EXPECT_EQ(to_key_code(SDLK_A), key_code::a);
    EXPECT_EQ(to_key_code(SDLK_W), key_code::w);
    EXPECT_EQ(to_key_code(SDLK_S), key_code::s);
    EXPECT_EQ(to_key_code(SDLK_D), key_code::d);
    EXPECT_EQ(to_key_code(SDLK_Z), key_code::z);
    EXPECT_EQ(to_key_code(SDLK_0), key_code::num_0);
    EXPECT_EQ(to_key_code(SDLK_9), key_code::num_9);
    EXPECT_EQ(to_key_code(SDLK_SPACE), key_code::space);
}

TEST(sdl_input, every_letter_maps_to_a_distinct_key)
{
    // The letter block is contiguous in both enums; walk it and check the
    // mapping is one-to-one rather than collapsing onto a shared value.
    for (SDL_Keycode key = SDLK_A; key <= SDLK_Z; ++key)
    {
        const key_code code = to_key_code(key);
        EXPECT_NE(code, key_code::unknown) << "key " << key;
        EXPECT_EQ(static_cast<int>(code), static_cast<int>(key_code::a) + static_cast<int>(key - SDLK_A))
            << "key " << key;
    }
}

TEST(sdl_input, function_and_navigation_keys_map)
{
    EXPECT_EQ(to_key_code(SDLK_F1), key_code::f1);
    EXPECT_EQ(to_key_code(SDLK_F12), key_code::f12);
    EXPECT_EQ(to_key_code(SDLK_RETURN), key_code::enter);
    EXPECT_EQ(to_key_code(SDLK_ESCAPE), key_code::escape);
    EXPECT_EQ(to_key_code(SDLK_BACKSPACE), key_code::backspace);
    EXPECT_EQ(to_key_code(SDLK_TAB), key_code::tab);
    EXPECT_EQ(to_key_code(SDLK_INSERT), key_code::insert);
    EXPECT_EQ(to_key_code(SDLK_DELETE), key_code::del);
    EXPECT_EQ(to_key_code(SDLK_HOME), key_code::home);
    EXPECT_EQ(to_key_code(SDLK_END), key_code::end);
    EXPECT_EQ(to_key_code(SDLK_PAGEUP), key_code::page_up);
    EXPECT_EQ(to_key_code(SDLK_PAGEDOWN), key_code::page_down);
    EXPECT_EQ(to_key_code(SDLK_LEFT), key_code::left);
    EXPECT_EQ(to_key_code(SDLK_RIGHT), key_code::right);
    EXPECT_EQ(to_key_code(SDLK_UP), key_code::up);
    EXPECT_EQ(to_key_code(SDLK_DOWN), key_code::down);
}

TEST(sdl_input, left_and_right_modifiers_are_distinct)
{
    EXPECT_EQ(to_key_code(SDLK_LSHIFT), key_code::left_shift);
    EXPECT_EQ(to_key_code(SDLK_RSHIFT), key_code::right_shift);
    EXPECT_EQ(to_key_code(SDLK_LCTRL), key_code::left_ctrl);
    EXPECT_EQ(to_key_code(SDLK_RCTRL), key_code::right_ctrl);
    EXPECT_EQ(to_key_code(SDLK_LALT), key_code::left_alt);
    EXPECT_EQ(to_key_code(SDLK_RALT), key_code::right_alt);
    EXPECT_EQ(to_key_code(SDLK_LGUI), key_code::left_gui);
    EXPECT_EQ(to_key_code(SDLK_RGUI), key_code::right_gui);

    EXPECT_NE(to_key_code(SDLK_LSHIFT), to_key_code(SDLK_RSHIFT));
    EXPECT_NE(to_key_code(SDLK_LCTRL), to_key_code(SDLK_RCTRL));
}

TEST(sdl_input, punctuation_locks_and_keypad_map)
{
    EXPECT_EQ(to_key_code(SDLK_MINUS), key_code::minus);
    EXPECT_EQ(to_key_code(SDLK_EQUALS), key_code::equals);
    EXPECT_EQ(to_key_code(SDLK_LEFTBRACKET), key_code::left_bracket);
    EXPECT_EQ(to_key_code(SDLK_RIGHTBRACKET), key_code::right_bracket);
    EXPECT_EQ(to_key_code(SDLK_BACKSLASH), key_code::backslash);
    EXPECT_EQ(to_key_code(SDLK_SEMICOLON), key_code::semicolon);
    EXPECT_EQ(to_key_code(SDLK_APOSTROPHE), key_code::apostrophe);
    EXPECT_EQ(to_key_code(SDLK_GRAVE), key_code::grave);
    EXPECT_EQ(to_key_code(SDLK_COMMA), key_code::comma);
    EXPECT_EQ(to_key_code(SDLK_PERIOD), key_code::period);
    EXPECT_EQ(to_key_code(SDLK_SLASH), key_code::slash);

    EXPECT_EQ(to_key_code(SDLK_CAPSLOCK), key_code::caps_lock);
    EXPECT_EQ(to_key_code(SDLK_NUMLOCKCLEAR), key_code::num_lock);
    EXPECT_EQ(to_key_code(SDLK_SCROLLLOCK), key_code::scroll_lock);
    EXPECT_EQ(to_key_code(SDLK_PRINTSCREEN), key_code::print_screen);
    EXPECT_EQ(to_key_code(SDLK_PAUSE), key_code::pause);
    EXPECT_EQ(to_key_code(SDLK_APPLICATION), key_code::menu);

    EXPECT_EQ(to_key_code(SDLK_KP_0), key_code::keypad_0);
    EXPECT_EQ(to_key_code(SDLK_KP_9), key_code::keypad_9);
    EXPECT_EQ(to_key_code(SDLK_KP_DIVIDE), key_code::keypad_divide);
    EXPECT_EQ(to_key_code(SDLK_KP_MULTIPLY), key_code::keypad_multiply);
    EXPECT_EQ(to_key_code(SDLK_KP_MINUS), key_code::keypad_minus);
    EXPECT_EQ(to_key_code(SDLK_KP_PLUS), key_code::keypad_plus);
    EXPECT_EQ(to_key_code(SDLK_KP_ENTER), key_code::keypad_enter);
    EXPECT_EQ(to_key_code(SDLK_KP_PERIOD), key_code::keypad_period);
    EXPECT_EQ(to_key_code(SDLK_KP_EQUALS), key_code::keypad_equals);
}

TEST(sdl_input, keypad_enter_is_not_the_main_enter)
{
    EXPECT_NE(to_key_code(SDLK_KP_ENTER), to_key_code(SDLK_RETURN));
}

TEST(sdl_input, unnamed_keys_map_to_unknown_not_garbage)
{
    EXPECT_EQ(to_key_code(SDLK_UNKNOWN), key_code::unknown);
    // Keys SDL knows but the engine does not name.
    EXPECT_EQ(to_key_code(SDLK_F24), key_code::unknown);
    EXPECT_EQ(to_key_code(SDLK_VOLUMEUP), key_code::unknown);
    EXPECT_EQ(to_key_code(SDLK_KP_HEXADECIMAL), key_code::unknown);
    // A value that is not an SDL keycode at all (the old cast would have
    // produced an out-of-range enumerator here).
    EXPECT_EQ(to_key_code(static_cast<SDL_Keycode>(0x12345678u)), key_code::unknown);
}

TEST(sdl_input, no_named_key_maps_to_the_count_sentinel)
{
    // Sweep the whole keycode range the engine names from and check nothing
    // lands on the sentinel or outside the enum.
    const auto check = [](SDL_Keycode key)
    {
        const int value = static_cast<int>(to_key_code(key));
        EXPECT_GE(value, static_cast<int>(key_code::unknown)) << "key " << key;
        EXPECT_LT(value, static_cast<int>(key_code::count)) << "key " << key;
    };
    for (SDL_Keycode key = 0; key <= 0x7f; ++key)
    {
        check(key);
    }
    for (SDL_Keycode key = SDLK_CAPSLOCK; key <= SDLK_RGUI; ++key)
    {
        check(key);
    }
}

// -- Mouse --------------------------------------------------------------------

TEST(sdl_input, mouse_buttons_map_including_the_extra_buttons)
{
    EXPECT_EQ(to_mouse_key_code(SDL_BUTTON_LEFT), mouse_key_code::left);
    EXPECT_EQ(to_mouse_key_code(SDL_BUTTON_RIGHT), mouse_key_code::right);
    EXPECT_EQ(to_mouse_key_code(SDL_BUTTON_MIDDLE), mouse_key_code::middle);
    EXPECT_EQ(to_mouse_key_code(SDL_BUTTON_X1), mouse_key_code::x1);
    EXPECT_EQ(to_mouse_key_code(SDL_BUTTON_X2), mouse_key_code::x2);
}

TEST(sdl_input, unnamed_mouse_buttons_yield_no_code)
{
    // Button 0 is never reported by SDL; 6 and above are beyond the named set.
    EXPECT_EQ(to_mouse_key_code(0), std::nullopt);
    EXPECT_EQ(to_mouse_key_code(6), std::nullopt);
    EXPECT_EQ(to_mouse_key_code(255), std::nullopt);
}

// -- Gamepad ------------------------------------------------------------------

TEST(sdl_input, gamepad_buttons_map)
{
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_SOUTH), gamepad_button_code::south);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_EAST), gamepad_button_code::east);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_WEST), gamepad_button_code::west);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_NORTH), gamepad_button_code::north);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_BACK), gamepad_button_code::back);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_GUIDE), gamepad_button_code::guide);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_START), gamepad_button_code::start);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_LEFT_STICK), gamepad_button_code::left_stick);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_RIGHT_STICK), gamepad_button_code::right_stick);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), gamepad_button_code::left_shoulder);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), gamepad_button_code::right_shoulder);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_DPAD_UP), gamepad_button_code::dpad_up);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_DPAD_DOWN), gamepad_button_code::dpad_down);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_DPAD_LEFT), gamepad_button_code::dpad_left);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_DPAD_RIGHT), gamepad_button_code::dpad_right);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_MISC1), gamepad_button_code::misc1);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1), gamepad_button_code::right_paddle1);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1), gamepad_button_code::left_paddle1);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2), gamepad_button_code::right_paddle2);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_LEFT_PADDLE2), gamepad_button_code::left_paddle2);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_TOUCHPAD), gamepad_button_code::touchpad);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_MISC6), gamepad_button_code::misc6);
}

TEST(sdl_input, every_sdl_gamepad_button_has_an_engine_name)
{
    for (int button = SDL_GAMEPAD_BUTTON_SOUTH; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
    {
        EXPECT_NE(to_gamepad_button_code(static_cast<SDL_GamepadButton>(button)), gamepad_button_code::unknown)
            << "button " << button;
    }
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_INVALID), gamepad_button_code::unknown);
    EXPECT_EQ(to_gamepad_button_code(SDL_GAMEPAD_BUTTON_COUNT), gamepad_button_code::unknown);
}

TEST(sdl_input, gamepad_axes_map)
{
    EXPECT_EQ(to_gamepad_axis_code(SDL_GAMEPAD_AXIS_LEFTX), gamepad_axis_code::left_x);
    EXPECT_EQ(to_gamepad_axis_code(SDL_GAMEPAD_AXIS_LEFTY), gamepad_axis_code::left_y);
    EXPECT_EQ(to_gamepad_axis_code(SDL_GAMEPAD_AXIS_RIGHTX), gamepad_axis_code::right_x);
    EXPECT_EQ(to_gamepad_axis_code(SDL_GAMEPAD_AXIS_RIGHTY), gamepad_axis_code::right_y);
    EXPECT_EQ(to_gamepad_axis_code(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), gamepad_axis_code::left_trigger);
    EXPECT_EQ(to_gamepad_axis_code(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), gamepad_axis_code::right_trigger);
    EXPECT_EQ(to_gamepad_axis_code(SDL_GAMEPAD_AXIS_INVALID), gamepad_axis_code::unknown);
    EXPECT_EQ(to_gamepad_axis_code(SDL_GAMEPAD_AXIS_COUNT), gamepad_axis_code::unknown);
}

TEST(sdl_input, axis_normalisation_hits_the_unit_bounds_exactly)
{
    EXPECT_FLOAT_EQ(normalize_axis(0), 0.0f);
    EXPECT_FLOAT_EQ(normalize_axis(32767), 1.0f);
    EXPECT_FLOAT_EQ(normalize_axis(-32767), -1.0f);
    // The negative extreme is one further out; it clamps rather than
    // overshooting to -1.00003.
    EXPECT_FLOAT_EQ(normalize_axis(-32768), -1.0f);
    // The remaining range is linear.
    EXPECT_NEAR(normalize_axis(16384), 0.5f, 1e-4f);
    EXPECT_NEAR(normalize_axis(-16384), -0.5f, 1e-4f);
}
