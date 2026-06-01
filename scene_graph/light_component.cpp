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

#include <scene_graph/light_component.hpp>

#include <infrastructure/math/math.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <scene_graph/node.hpp>

scene_graph::light_component::light_component(std::unique_ptr<rendering_engine::light> light)
    : m_light{std::move(light)}
{
}

void scene_graph::light_component::on_update(node& owner)
{
    if (!m_light)
    {
        return;
    }

    const infrastructure::math::mat4 world = owner.world_matrix();

    switch (m_light->type())
    {
    case rendering_engine::light_type::point:
        // Column 3 of the world matrix is the node's world translation.
        static_cast<rendering_engine::point_light&>(*m_light).position =
            infrastructure::math::vec3{world.m[12], world.m[13], world.m[14]};
        break;
    case rendering_engine::light_type::directional:
    {
        // Travel along the node's world forward (-Z), matching the forward
        // convention of util::transform. Column 2 is the node's world +Z axis.
        const infrastructure::math::vec3 forward{-world.m[8], -world.m[9], -world.m[10]};
        static_cast<rendering_engine::directional_light&>(*m_light).direction =
            infrastructure::math::normalize(forward);
        break;
    }
    case rendering_engine::light_type::ambient:
        // No spatial term to track.
        break;
    }
}
