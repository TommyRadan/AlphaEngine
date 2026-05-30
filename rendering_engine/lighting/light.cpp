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
    } // namespace

    light::light(light_type type) : m_type(type)
    {
        light_registry().push_back(this);
    }

    light::~light()
    {
        auto& lights = light_registry();
        lights.erase(std::remove(lights.begin(), lights.end(), this), lights.end());
    }

    light_type light::type() const noexcept
    {
        return m_type;
    }

    const std::vector<light*>& registered_lights()
    {
        return light_registry();
    }
} // namespace rendering_engine
