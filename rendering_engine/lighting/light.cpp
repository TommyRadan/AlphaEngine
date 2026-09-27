// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/lighting/light.hpp>

#include <algorithm>

namespace rendering_engine
{
    namespace
    {
        // Function-local static so the registry is alive before any
        // light's static/global constructor runs and survives until the
        // last light is destroyed — game modules may create lights at
        // static-init time through the module pattern.
        std::vector<light*>& light_registry()
        {
            static std::vector<light*> lights;
            return lights;
        }

        void unregister_light(light* l)
        {
            auto& lights = light_registry();
            lights.erase(std::remove(lights.begin(), lights.end(), l), lights.end());
        }
    } // namespace

    light::light(light_type type) : m_type(type)
    {
        light_registry().push_back(this);
    }

    light::~light()
    {
        // A no-op for a light that was disabled at the time.
        unregister_light(this);
    }

    light_type light::type() const noexcept
    {
        return m_type;
    }

    void light::set_enabled(bool enabled)
    {
        if (enabled == m_enabled)
        {
            return;
        }
        m_enabled = enabled;
        if (enabled)
        {
            light_registry().push_back(this);
        }
        else
        {
            unregister_light(this);
        }
    }

    bool light::is_enabled() const noexcept
    {
        return m_enabled;
    }

    const std::vector<light*>& registered_lights()
    {
        return light_registry();
    }
} // namespace rendering_engine
