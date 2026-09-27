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
