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

#include <rendering_engine/debug/helper.hpp>

#include <algorithm>

#include <control/engine.hpp>
#include <rendering_engine/rendering_engine.hpp>

namespace rendering_engine::debug
{
    namespace
    {
        // Process-wide list of live helpers, in construction order. Holds
        // non-owning back-pointers; the debug UI walks it to toggle each
        // gizmo. Function-local so it is constructed on first use,
        // mirroring registered_lights().
        std::vector<helper*>& helper_registry()
        {
            static std::vector<helper*> helpers;
            return helpers;
        }
    } // namespace

    helper::helper(const char* name, helper_layer layer) : m_name(name), m_layer(layer)
    {
        helper_registry().push_back(this);

        auto& renderer = *control::current_engine().renderer;
        if (m_layer == helper_layer::scene)
        {
            renderer.register_scene_renderable(this);
        }
        else
        {
            renderer.register_debug_renderable(this);
        }
    }

    helper::~helper()
    {
        auto& renderer = *control::current_engine().renderer;
        if (m_layer == helper_layer::scene)
        {
            renderer.unregister_scene_renderable(this);
        }
        else
        {
            renderer.unregister_debug_renderable(this);
        }

        auto& helpers = helper_registry();
        helpers.erase(std::remove(helpers.begin(), helpers.end(), this), helpers.end());
    }

    const char* helper::name() const noexcept
    {
        return m_name;
    }

    infrastructure::math::vec3 helper::to_rgb(const util::color& c)
    {
        return infrastructure::math::vec3{
            static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f, static_cast<float>(c.b) / 255.0f};
    }

    const std::vector<helper*>& registered_helpers()
    {
        return helper_registry();
    }
} // namespace rendering_engine::debug
