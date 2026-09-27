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

#include <runtime/components/light_component.hpp>

#include <core/math/math.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <runtime/node.hpp>

namespace
{
    // A forward axis shorter than this carries no direction (a zero-scale
    // node); the light keeps the direction it already has.
    constexpr float degenerate_length = 1e-6f;
} // namespace

runtime::light_component::light_component(std::unique_ptr<rendering_engine::light> light) : m_light{std::move(light)} {}

runtime::light_component runtime::light_component::clone() const
{
    if (!m_light)
    {
        return light_component{};
    }

    std::unique_ptr<rendering_engine::light> copy;
    switch (m_light->type())
    {
    case rendering_engine::light_type::ambient:
        copy = std::make_unique<rendering_engine::ambient_light>();
        break;
    case rendering_engine::light_type::directional:
    {
        const auto& source = static_cast<const rendering_engine::directional_light&>(*m_light);
        auto directional = std::make_unique<rendering_engine::directional_light>();
        directional->direction = source.direction;
        directional->cast_shadow = source.cast_shadow;
        copy = std::move(directional);
        break;
    }
    case rendering_engine::light_type::point:
    {
        const auto& source = static_cast<const rendering_engine::point_light&>(*m_light);
        auto point = std::make_unique<rendering_engine::point_light>();
        point->position = source.position;
        point->range = source.range;
        point->constant_attenuation = source.constant_attenuation;
        point->linear_attenuation = source.linear_attenuation;
        point->quadratic_attenuation = source.quadratic_attenuation;
        point->cast_shadow = source.cast_shadow;
        copy = std::move(point);
        break;
    }
    }
    if (!copy)
    {
        return light_component{};
    }
    copy->color = m_light->color;
    copy->intensity = m_light->intensity;
    return light_component{std::move(copy)};
}

void runtime::light_component::on_active_changed(node& owner, bool active)
{
    (void)owner;
    if (m_light)
    {
        m_light->set_enabled(active);
    }
}

void runtime::light_component::on_update(node& owner)
{
    if (!m_light)
    {
        return;
    }

    const core::math::mat4 world = owner.world_matrix();

    switch (m_light->type())
    {
    case rendering_engine::light_type::point:
        // Column 3 of the world matrix is the node's world translation.
        static_cast<rendering_engine::point_light&>(*m_light).position =
            core::math::vec3{world.m[12], world.m[13], world.m[14]};
        break;
    case rendering_engine::light_type::directional:
    {
        // Travel along the node's world forward: +X in the engine convention
        // (core/math/math.hpp), matching util::transform::get_forward. Column
        // 0 is the node's world +X axis; its length is the node's x scale, so
        // a zero-scale node yields no direction and the light keeps its last
        // one instead of taking a NaN into the lights UBO.
        const core::math::vec3 forward{world.m[0], world.m[1], world.m[2]};
        const float forward_length = core::math::length(forward);
        if (forward_length > degenerate_length)
        {
            static_cast<rendering_engine::directional_light&>(*m_light).direction = forward / forward_length;
        }
        break;
    }
    case rendering_engine::light_type::ambient:
        // No spatial term to track.
        break;
    }
}
