// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/physics/physics_body.hpp>

#include <algorithm>

namespace runtime::physics
{
    listener_id contact_listeners::add_collision(collision_listener listener)
    {
        if (!listener)
        {
            return 0;
        }
        const listener_id id = next_id++;
        collision.emplace_back(id, std::move(listener));
        return id;
    }

    listener_id contact_listeners::add_trigger(trigger_listener listener)
    {
        if (!listener)
        {
            return 0;
        }
        const listener_id id = next_id++;
        trigger.emplace_back(id, std::move(listener));
        return id;
    }

    bool contact_listeners::remove(listener_id id)
    {
        const auto matches = [id](const auto& entry) { return entry.first == id; };
        const auto collision_it = std::find_if(collision.begin(), collision.end(), matches);
        if (collision_it != collision.end())
        {
            collision.erase(collision_it);
            return true;
        }
        const auto trigger_it = std::find_if(trigger.begin(), trigger.end(), matches);
        if (trigger_it != trigger.end())
        {
            trigger.erase(trigger_it);
            return true;
        }
        return false;
    }

    void rigidbody_state::clear_pending()
    {
        force = core::math::vec3{};
        torque = core::math::vec3{};
        impulse = core::math::vec3{};
        angular_impulse = core::math::vec3{};
        forces_at.clear();
        impulses_at.clear();
        wake_requested = false;
    }
} // namespace runtime::physics
