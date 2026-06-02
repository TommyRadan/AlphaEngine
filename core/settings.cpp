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

#include <cstdlib>
#include <cstring>

#include <infrastructure/log.hpp>
#include <infrastructure/settings.hpp>
#include <infrastructure/version.hpp>
#include <SDL3/SDL.h>

namespace
{
    const char* graphics_backend_name(graphics_backend b) noexcept
    {
        switch (b)
        {
        case graphics_backend::opengl:
            return "opengl";
        case graphics_backend::vulkan:
            return "vulkan";
        }
        return "unknown";
    }

    graphics_backend parse_graphics_backend()
    {
        const char* value = std::getenv("ALPHAENGINE_GRAPHICS_BACKEND");
        if (value == nullptr || value[0] == '\0')
        {
            return graphics_backend::opengl;
        }
        if (std::strcmp(value, "opengl") == 0)
        {
            return graphics_backend::opengl;
        }
        if (std::strcmp(value, "vulkan") == 0)
        {
            return graphics_backend::vulkan;
        }
        LOG_ERR("settings: unknown ALPHAENGINE_GRAPHICS_BACKEND='%s'; falling back to opengl", value);
        return graphics_backend::opengl;
    }
} // namespace

settings::settings()
{
    // Settings are currently sourced from compile-time defaults / the current
    // display mode rather than a config file. When a file-based loader is
    // introduced, log the resolved config path at INFO here. For now we log
    // "compiled-in defaults" as the resolved source.
    LOG_INF("Settings: loading from compiled-in defaults (no config file on disk)");

#if _DEBUG
    // Debug runs windowed and roomy: a larger canvas leaves space for the
    // ImGui debug panels (FPS overlay, settings inspector) without
    // crowding the scene.
    m_window_width = 1600;
    m_window_height = 900;
    m_window_type = win_type::win_type_windowed;
#else
    const SDL_DisplayMode* displayMode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    if (displayMode == nullptr)
    {
        LOG_WRN("SDL_GetCurrentDisplayMode failed (%s); falling back to 1280x720 windowed", SDL_GetError());
        m_window_width = 1280;
        m_window_height = 720;
        m_window_type = win_type::win_type_windowed;
    }
    else
    {
        m_window_width = displayMode->w;
        m_window_height = displayMode->h;
        m_window_type = win_type::win_type_fullscreen;
    }
#endif

    m_window_name = std::string{"AlphaEngine v"} + infrastructure::version::get_version();
    m_is_double_buffered = true;
    // Vsync off in both configurations: the frame rate is left uncapped.
    m_is_vsync_enabled = false;
    m_field_of_view = 70.0f;
    m_mouse_sensitivity = 0.005f;
    m_is_mouse_reversed = false;
    m_graphics_backend = parse_graphics_backend();

    LOG_INF("Settings parsed: window=%ux%u type=%d double_buffered=%d vsync=%d fov=%.1f "
            "mouse_sensitivity=%.4f mouse_reversed=%d graphics_backend=%s name='%s'",
            m_window_width,
            m_window_height,
            static_cast<int>(m_window_type),
            m_is_double_buffered ? 1 : 0,
            m_is_vsync_enabled ? 1 : 0,
            m_field_of_view,
            m_mouse_sensitivity,
            m_is_mouse_reversed ? 1 : 0,
            graphics_backend_name(m_graphics_backend),
            m_window_name.c_str());
}

const unsigned int settings::get_window_width() const noexcept
{
    return m_window_width;
}

const unsigned int settings::get_window_height() const noexcept
{
    return m_window_height;
}

const char* settings::get_window_name() const noexcept
{
    return m_window_name.c_str();
}

const win_type settings::get_window_type() const noexcept
{
    return m_window_type;
}

const bool settings::is_double_buffered() const noexcept
{
    return m_is_double_buffered;
}

const bool settings::is_vsync_enabled() const noexcept
{
    return m_is_vsync_enabled;
}

const float settings::get_field_of_view() const noexcept
{
    return m_field_of_view;
}

const float settings::get_mouse_sensitivity() const noexcept
{
    return m_mouse_sensitivity;
}

const float settings::get_aspect_ratio() const noexcept
{
    return float(m_window_width) / m_window_height;
}

const bool settings::is_mouse_reversed() const noexcept
{
    return m_is_mouse_reversed;
}

const graphics_backend settings::get_graphics_backend() const noexcept
{
    return m_graphics_backend;
}
