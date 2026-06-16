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

#include <core/log.hpp>
#include <core/settings.hpp>
#include <core/version.hpp>
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
            return graphics_backend::vulkan;
        }
        if (std::strcmp(value, "opengl") == 0)
        {
            return graphics_backend::opengl;
        }
        if (std::strcmp(value, "vulkan") == 0)
        {
            return graphics_backend::vulkan;
        }
        LOG_ERR("settings: unknown ALPHAENGINE_GRAPHICS_BACKEND='%s'; falling back to vulkan", value);
        return graphics_backend::vulkan;
    }

    bool parse_temporal_aa()
    {
        const char* value = std::getenv("ALPHAENGINE_TAA");
        if (value == nullptr || value[0] == '\0')
        {
            return true;
        }
        if (std::strcmp(value, "0") == 0 || std::strcmp(value, "off") == 0 || std::strcmp(value, "false") == 0)
        {
            return false;
        }
        if (std::strcmp(value, "1") == 0 || std::strcmp(value, "on") == 0 || std::strcmp(value, "true") == 0)
        {
            return true;
        }
        LOG_ERR("settings: unknown ALPHAENGINE_TAA='%s'; falling back to enabled", value);
        return true;
    }
} // namespace

float window_settings::aspect_ratio() const noexcept
{
    return float(width) / height;
}

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
    window.width = 1600;
    window.height = 900;
    window.type = win_type::win_type_windowed;
#else
    const SDL_DisplayMode* displayMode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    if (displayMode == nullptr)
    {
        LOG_WRN("SDL_GetCurrentDisplayMode failed (%s); falling back to 1280x720 windowed", SDL_GetError());
        window.width = 1280;
        window.height = 720;
        window.type = win_type::win_type_windowed;
    }
    else
    {
        window.width = displayMode->w;
        window.height = displayMode->h;
        window.type = win_type::win_type_fullscreen;
    }
#endif

    window.name = std::string{"AlphaEngine v"} + core::version::get_version();
    window.double_buffered = true;
    // Vsync off in both configurations: the frame rate is left uncapped.
    window.vsync = false;
    graphics.backend = parse_graphics_backend();
    graphics.temporal_aa = parse_temporal_aa();
    camera.field_of_view = 70.0f;
    input.mouse_sensitivity = 0.005f;
    input.mouse_reversed = false;

    LOG_INF("Settings parsed: window=%ux%u type=%d double_buffered=%d vsync=%d fov=%.1f "
            "mouse_sensitivity=%.4f mouse_reversed=%d graphics_backend=%s temporal_aa=%d name='%s'",
            window.width,
            window.height,
            static_cast<int>(window.type),
            window.double_buffered ? 1 : 0,
            window.vsync ? 1 : 0,
            camera.field_of_view,
            input.mouse_sensitivity,
            input.mouse_reversed ? 1 : 0,
            graphics_backend_name(graphics.backend),
            graphics.temporal_aa ? 1 : 0,
            window.name.c_str());
}
