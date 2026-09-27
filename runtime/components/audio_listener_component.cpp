// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/components/audio_listener_component.hpp>

#include <core/audio/audio.hpp>
#include <core/math/math.hpp>
#include <runtime/engine.hpp>
#include <runtime/node.hpp>

namespace
{
    // A +Y column shorter than this carries no direction (a zero-scale
    // node); fall back to the unrotated convention rather than divide by ~0.
    constexpr float degenerate_length = 1e-6f;

    // World-space right axis of a node's world matrix: the engine's right is
    // -Y given +X forward and +Z up (core::math::world_right; see also
    // core::transform::get_right(), which applies the same convention to the
    // local/parent frame), so this negates the world matrix's +Y column
    // rather than reading get_right(), which is only correct in the parent's
    // frame — a nested, rotated parent would otherwise skew the pan axis.
    // Guarded exactly like light_component guards its forward column.
    core::math::vec3 world_right_axis(const core::math::mat4& world)
    {
        const core::math::vec3 y_axis{world.m[4], world.m[5], world.m[6]};
        const float len = core::math::length(y_axis);
        return len > degenerate_length ? (y_axis / -len) : core::math::world_right;
    }
} // namespace

void runtime::audio_listener_component::on_attach(node& owner)
{
    core::audio* audio = runtime::current_engine().audio.get();
    if (audio == nullptr)
    {
        return;
    }
    m_token = audio->attach_listener();
    audio->set_listener_transform(m_token, owner.world_position(), world_right_axis(owner.world_matrix()));
}

void runtime::audio_listener_component::on_update(node& owner)
{
    if (m_token == 0)
    {
        return;
    }
    if (core::audio* audio = runtime::current_engine().audio.get())
    {
        audio->set_listener_transform(m_token, owner.world_position(), world_right_axis(owner.world_matrix()));
    }
}

void runtime::audio_listener_component::on_destroy()
{
    if (m_token == 0)
    {
        return;
    }
    if (core::audio* audio = runtime::current_engine().audio.get())
    {
        audio->detach_listener(m_token);
    }
    m_token = 0;
}

void runtime::audio_listener_component::on_active_changed(node& owner, bool active)
{
    (void)owner;
    if (m_token == 0)
    {
        return;
    }
    if (core::audio* audio = runtime::current_engine().audio.get())
    {
        audio->set_listener_enabled(m_token, active);
    }
}

runtime::audio_listener_component runtime::audio_listener_component::clone() const
{
    return audio_listener_component{};
}
