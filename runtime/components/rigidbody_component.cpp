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

#define LOG_CATEGORY "physics"

#include <runtime/components/rigidbody_component.hpp>

#include <utility>

#include <core/log.hpp>
#include <runtime/engine.hpp>
#include <runtime/physics/physics_world.hpp>

namespace runtime
{
    rigidbody_component::rigidbody_component() : m_state{std::make_unique<physics::rigidbody_state>()} {}

    rigidbody_component::rigidbody_component(physics::body_type type, float mass) : rigidbody_component()
    {
        m_state->settings.type = type;
        m_state->settings.mass = mass;
    }

    rigidbody_component::rigidbody_component(const physics::rigidbody_settings& settings) : rigidbody_component()
    {
        m_state->settings = settings;
    }

    rigidbody_component::~rigidbody_component()
    {
        release();
    }

    rigidbody_component& rigidbody_component::operator=(rigidbody_component&& other) noexcept
    {
        if (this != &other)
        {
            release();
            m_state = std::move(other.m_state);
        }
        return *this;
    }

    void rigidbody_component::release()
    {
        // on_destroy has normally unregistered already; this covers a
        // component dropped without it.
        if (m_state != nullptr && m_state->attached != nullptr)
        {
            m_state->attached->detach(*m_state);
        }
    }

    physics::rigidbody_state& rigidbody_component::state()
    {
        if (m_state == nullptr)
        {
            m_state = std::make_unique<physics::rigidbody_state>();
        }
        return *m_state;
    }

    const physics::rigidbody_state& rigidbody_component::state() const
    {
        static const physics::rigidbody_state empty{};
        return m_state != nullptr ? *m_state : empty;
    }

    void rigidbody_component::on_attach(node& owner)
    {
        physics::world* world = runtime::current_engine().physics.get();
        if (world == nullptr)
        {
            LOG_WRN("runtime::rigidbody_component: the engine has no physics world; the body is not simulated");
            return;
        }
        world->attach(owner, state());
    }

    void rigidbody_component::on_destroy()
    {
        release();
    }

    void rigidbody_component::on_active_changed(node& owner, bool active)
    {
        physics::rigidbody_state& current = state();
        current.active = active;
        if (current.attached != nullptr)
        {
            current.attached->refresh_active(owner);
        }
    }

    rigidbody_component rigidbody_component::clone() const
    {
        rigidbody_component copy{state().settings};
        copy.m_state->linear_velocity = state().linear_velocity;
        copy.m_state->angular_velocity = state().angular_velocity;
        return copy;
    }

    physics::rigidbody_settings rigidbody_component::settings() const
    {
        return state().settings;
    }

    void rigidbody_component::set_settings(const physics::rigidbody_settings& settings)
    {
        state().settings = settings;
    }

    physics::body_type rigidbody_component::type() const
    {
        return state().settings.type;
    }

    void rigidbody_component::set_type(physics::body_type type)
    {
        state().settings.type = type;
    }

    float rigidbody_component::mass() const
    {
        return state().settings.mass;
    }

    void rigidbody_component::set_mass(float mass)
    {
        state().settings.mass = mass;
    }

    float rigidbody_component::friction() const
    {
        return state().settings.friction;
    }

    void rigidbody_component::set_friction(float friction)
    {
        state().settings.friction = friction;
    }

    float rigidbody_component::restitution() const
    {
        return state().settings.restitution;
    }

    void rigidbody_component::set_restitution(float restitution)
    {
        state().settings.restitution = restitution;
    }

    float rigidbody_component::linear_damping() const
    {
        return state().settings.linear_damping;
    }

    void rigidbody_component::set_linear_damping(float damping)
    {
        state().settings.linear_damping = damping;
    }

    float rigidbody_component::angular_damping() const
    {
        return state().settings.angular_damping;
    }

    void rigidbody_component::set_angular_damping(float damping)
    {
        state().settings.angular_damping = damping;
    }

    float rigidbody_component::gravity_scale() const
    {
        return state().settings.gravity_scale;
    }

    void rigidbody_component::set_gravity_scale(float scale)
    {
        state().settings.gravity_scale = scale;
    }

    core::math::vec3 rigidbody_component::linear_velocity() const
    {
        return state().linear_velocity;
    }

    void rigidbody_component::set_linear_velocity(const core::math::vec3& velocity)
    {
        physics::rigidbody_state& current = state();
        current.linear_velocity = velocity;
        current.velocity_dirty = true;
    }

    core::math::vec3 rigidbody_component::angular_velocity() const
    {
        return state().angular_velocity;
    }

    void rigidbody_component::set_angular_velocity(const core::math::vec3& velocity)
    {
        physics::rigidbody_state& current = state();
        current.angular_velocity = velocity;
        current.velocity_dirty = true;
    }

    void rigidbody_component::add_force(const core::math::vec3& force)
    {
        state().force += force;
    }

    void rigidbody_component::add_force_at(const core::math::vec3& force, const core::math::vec3& point)
    {
        state().forces_at.push_back(physics::point_force{force, point});
    }

    void rigidbody_component::add_torque(const core::math::vec3& torque)
    {
        state().torque += torque;
    }

    void rigidbody_component::add_impulse(const core::math::vec3& impulse)
    {
        state().impulse += impulse;
    }

    void rigidbody_component::add_impulse_at(const core::math::vec3& impulse, const core::math::vec3& point)
    {
        state().impulses_at.push_back(physics::point_force{impulse, point});
    }

    void rigidbody_component::add_angular_impulse(const core::math::vec3& impulse)
    {
        state().angular_impulse += impulse;
    }

    bool rigidbody_component::is_sleeping() const
    {
        return state().sleeping;
    }

    void rigidbody_component::wake_up()
    {
        state().wake_requested = true;
    }

    physics::listener_id rigidbody_component::on_collision(physics::collision_listener listener)
    {
        return state().listeners.add_collision(std::move(listener));
    }

    physics::listener_id rigidbody_component::on_trigger(physics::trigger_listener listener)
    {
        return state().listeners.add_trigger(std::move(listener));
    }

    bool rigidbody_component::remove_listener(physics::listener_id id)
    {
        return state().listeners.remove(id);
    }
} // namespace runtime
