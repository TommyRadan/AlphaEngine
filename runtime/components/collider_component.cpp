// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#define LOG_CATEGORY "physics"

#include <runtime/components/collider_component.hpp>

#include <utility>

#include <core/log.hpp>
#include <runtime/engine.hpp>
#include <runtime/physics/physics_world.hpp>

namespace runtime
{
    collider_component::collider_component() : m_state{std::make_unique<physics::collider_state>()} {}

    collider_component::collider_component(const physics::collider_settings& settings) : collider_component()
    {
        m_state->settings = settings;
    }

    collider_component::~collider_component()
    {
        release();
    }

    collider_component& collider_component::operator=(collider_component&& other) noexcept
    {
        if (this != &other)
        {
            release();
            m_state = std::move(other.m_state);
        }
        return *this;
    }

    collider_component collider_component::box(const core::math::vec3& half_extents, const core::math::vec3& center)
    {
        physics::collider_settings settings;
        settings.shape = physics::collider_shape::box;
        settings.fit_to_mesh = false;
        settings.half_extents = half_extents;
        settings.center = center;
        return collider_component{settings};
    }

    collider_component collider_component::sphere(float radius, const core::math::vec3& center)
    {
        physics::collider_settings settings;
        settings.shape = physics::collider_shape::sphere;
        settings.fit_to_mesh = false;
        settings.radius = radius;
        settings.center = center;
        return collider_component{settings};
    }

    collider_component collider_component::capsule(float radius, float half_height, const core::math::vec3& center)
    {
        physics::collider_settings settings;
        settings.shape = physics::collider_shape::capsule;
        settings.fit_to_mesh = false;
        settings.radius = radius;
        settings.half_height = half_height;
        settings.center = center;
        return collider_component{settings};
    }

    collider_component collider_component::convex_hull(std::vector<core::math::vec3> points)
    {
        physics::collider_settings settings;
        settings.shape = physics::collider_shape::convex_hull;
        settings.fit_to_mesh = false;
        settings.points = std::move(points);
        return collider_component{settings};
    }

    collider_component collider_component::fitted(physics::collider_shape shape)
    {
        physics::collider_settings settings;
        settings.shape = shape;
        settings.fit_to_mesh = true;
        return collider_component{settings};
    }

    void collider_component::release()
    {
        // on_destroy has normally unregistered already; this covers a
        // component dropped without it.
        if (m_state != nullptr && m_state->attached != nullptr)
        {
            m_state->attached->detach(*m_state);
        }
    }

    physics::collider_state& collider_component::state()
    {
        if (m_state == nullptr)
        {
            m_state = std::make_unique<physics::collider_state>();
        }
        return *m_state;
    }

    const physics::collider_state& collider_component::state() const
    {
        static const physics::collider_state empty{};
        return m_state != nullptr ? *m_state : empty;
    }

    void collider_component::on_attach(node& owner)
    {
        physics::world* world = runtime::current_engine().physics.get();
        if (world == nullptr)
        {
            LOG_WRN("runtime::collider_component: the engine has no physics world; the shape does not collide");
            return;
        }
        world->attach(owner, state());
    }

    void collider_component::on_destroy()
    {
        release();
    }

    void collider_component::on_active_changed(node& owner, bool active)
    {
        physics::collider_state& current = state();
        current.active = active;
        if (current.attached != nullptr)
        {
            current.attached->refresh_active(owner);
        }
    }

    collider_component collider_component::clone() const
    {
        return collider_component{state().settings};
    }

    physics::collider_settings collider_component::settings() const
    {
        return state().settings;
    }

    void collider_component::set_settings(const physics::collider_settings& settings)
    {
        state().settings = settings;
    }

    physics::collider_shape collider_component::shape() const
    {
        return state().settings.shape;
    }

    bool collider_component::is_trigger() const
    {
        return state().settings.is_trigger;
    }

    void collider_component::set_trigger(bool trigger)
    {
        state().settings.is_trigger = trigger;
    }

    physics::listener_id collider_component::on_collision(physics::collision_listener listener)
    {
        return state().listeners.add_collision(std::move(listener));
    }

    physics::listener_id collider_component::on_trigger(physics::trigger_listener listener)
    {
        return state().listeners.add_trigger(std::move(listener));
    }

    bool collider_component::remove_listener(physics::listener_id id)
    {
        return state().listeners.remove(id);
    }
} // namespace runtime
