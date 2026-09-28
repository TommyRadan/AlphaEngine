// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/components/light_component.hpp>

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <rendering_engine/lighting/spot_light.hpp>
#include <rendering_engine/render_world.hpp>
#include <runtime/node.hpp>
#include <runtime/scene.hpp>

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
        directional->cast_shadow = source.cast_shadow;
        copy = std::move(directional);
        break;
    }
    case rendering_engine::light_type::point:
    {
        const auto& source = static_cast<const rendering_engine::point_light&>(*m_light);
        auto point = std::make_unique<rendering_engine::point_light>();
        point->range = source.range;
        point->constant_attenuation = source.constant_attenuation;
        point->linear_attenuation = source.linear_attenuation;
        point->quadratic_attenuation = source.quadratic_attenuation;
        point->cast_shadow = source.cast_shadow;
        copy = std::move(point);
        break;
    }
    case rendering_engine::light_type::spot:
    {
        const auto& source = static_cast<const rendering_engine::spot_light&>(*m_light);
        auto spot = std::make_unique<rendering_engine::spot_light>();
        spot->range = source.range;
        spot->constant_attenuation = source.constant_attenuation;
        spot->linear_attenuation = source.linear_attenuation;
        spot->quadratic_attenuation = source.quadratic_attenuation;
        spot->outer_angle = source.outer_angle;
        spot->inner_angle = source.inner_angle;
        spot->cast_shadow = source.cast_shadow;
        copy = std::move(spot);
        break;
    }
    }
    if (!copy)
    {
        LOG_WRN("runtime::light_component::clone: unhandled light_type %d; the clone owns no light",
                static_cast<int>(m_light->type()));
        return light_component{};
    }
    copy->color = m_light->color;
    copy->intensity = m_light->intensity;
    return light_component{std::move(copy)};
}

void runtime::light_component::on_attach(node& owner)
{
    if (!m_light)
    {
        return;
    }
    runtime::scene* scene = owner.scene();
    rendering_engine::render_world* world = scene != nullptr ? scene->world() : nullptr;
    if (world == nullptr)
    {
        LOG_WRN("runtime::light_component::on_attach: node has no scene render_world; the light has no proxy");
        return;
    }

    rendering_engine::light_proxy proxy{};
    rendering_engine::copy_light_settings(*m_light, proxy);
    // Where a node whose scale collapses its forward axis leaves the
    // direction: a spot light shines along +X, anything else straight down.
    proxy.direction = m_light->type() == rendering_engine::light_type::spot ? core::math::vec3{1.0f, 0.0f, 0.0f}
                                                                            : core::math::vec3{0.0f, 0.0f, -1.0f};
    m_placed_version = owner.transform.get_world_version();
    rendering_engine::place_light(owner.transform.get_world_matrix(), proxy);

    m_world = world;
    m_proxy = world->create_light(proxy, m_light->is_enabled());
}

void runtime::light_component::on_destroy()
{
    if (m_world != nullptr)
    {
        m_world->destroy_light(m_proxy);
    }
    m_world = nullptr;
    m_proxy = {};
}

void runtime::light_component::on_active_changed(node& owner, bool active)
{
    (void)owner;
    if (m_light)
    {
        m_light->set_enabled(active);
    }
    // The enabled list is the packing order, so the change reaches the
    // world now rather than at the next extraction: lights toggled in one
    // frame keep the order they were toggled in.
    if (m_world != nullptr)
    {
        m_world->set_light_enabled(m_proxy, active);
    }
}

void runtime::light_component::extract(const node& owner)
{
    if (m_world == nullptr || !m_light)
    {
        return;
    }
    rendering_engine::light_proxy* proxy = m_world->light(m_proxy);
    if (proxy == nullptr)
    {
        return;
    }

    rendering_engine::copy_light_settings(*m_light, *proxy);
    // An enabled flag toggled on the settings themselves (the inspector)
    // rather than through the node.
    m_world->set_light_enabled(m_proxy, m_light->is_enabled());

    const uint64_t version = owner.transform.get_world_version();
    if (version != m_placed_version)
    {
        rendering_engine::place_light(owner.transform.get_world_matrix(), *proxy);
        m_placed_version = version;
    }
}
