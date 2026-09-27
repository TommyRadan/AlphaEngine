// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/components/behavior_component.hpp>

#include <typeinfo>
#include <utility>

#include <core/event_engine.hpp>
#include <core/log.hpp>
#include <core/time.hpp>
#include <runtime/engine.hpp>
#include <runtime/scene.hpp>

runtime::behavior_component::behavior_component(std::unique_ptr<behavior> logic) noexcept : m_behavior{std::move(logic)}
{
}

void runtime::behavior_component::enable(behavior& logic)
{
    if (!logic.m_enabled)
    {
        logic.m_enabled = true;
        logic.on_enable();
    }
}

void runtime::behavior_component::disable(behavior& logic)
{
    if (logic.m_enabled)
    {
        logic.m_enabled = false;
        logic.on_disable();
    }
}

bool runtime::behavior_component::begin_update(behavior& logic)
{
    if (!logic.m_enabled || logic.m_owner == nullptr || logic.m_owner->is_destroy_pending())
    {
        return false;
    }
    if (!logic.m_started)
    {
        logic.m_started = true;
        logic.on_start();
    }
    // on_start cannot disable the behaviour on the spot (set_active is
    // deferred inside a traversal), but check rather than assume.
    return logic.m_enabled;
}

void runtime::behavior_component::fixed_step(behavior& logic, float delta_time)
{
    if (logic.m_owner == nullptr)
    {
        return;
    }
    // Mark the owner's scene as walked for the duration, as the scene update
    // does around on_update: the node APIs then refuse (debug) or defer
    // (release) structural changes, so the behaviour cannot, for one, free
    // itself while its hook is still running.
    scene::traversal_scope traversal{logic.m_owner->scene()};
    if (begin_update(logic))
    {
        logic.on_fixed_update(delta_time);
    }
}

void runtime::behavior_component::on_attach(node& owner)
{
    if (!m_behavior)
    {
        return;
    }

    m_behavior->m_owner = &owner;

    // The fixed step reaches behaviours through the core::frame the engine
    // emits once per drained step. The listener holds the heap behaviour,
    // which keeps its address however the pool moves this component, and is
    // dropped in on_destroy before the behaviour is deleted.
    behavior* logic = m_behavior.get();
    m_fixed_step = runtime::current_engine().events->subscribe<core::frame>([logic](const core::frame& step)
                                                                            { fixed_step(*logic, step.m_delta_time); });

    // A disabled node hides the component right after this (add_component
    // and the clone path both follow up with on_active_changed(false)), so
    // only an active one enables it here.
    if (owner.is_effective_active())
    {
        enable(*m_behavior);
    }
}

void runtime::behavior_component::on_update(node& owner)
{
    (void)owner;
    if (!m_behavior || !begin_update(*m_behavior))
    {
        return;
    }
    // The same real frame delta core::render_update carries this frame.
    m_behavior->on_update(static_cast<float>(runtime::current_engine().time->delta_time()));
}

void runtime::behavior_component::on_active_changed(node& owner, bool active)
{
    (void)owner;
    if (!m_behavior)
    {
        return;
    }
    if (active)
    {
        enable(*m_behavior);
    }
    else
    {
        disable(*m_behavior);
    }
}

void runtime::behavior_component::on_destroy()
{
    // No more fixed steps, whatever the hooks below do.
    m_fixed_step.reset();
    if (!m_behavior)
    {
        return;
    }
    disable(*m_behavior);
    m_behavior->on_destroy();
    // Delete it here, while the node and the scene it reached through are
    // still alive, rather than whenever the store frees the slot.
    m_behavior.reset();
}

std::optional<runtime::behavior_component> runtime::behavior_component::clone() const
{
    if (!m_behavior)
    {
        return behavior_component{};
    }
    std::unique_ptr<behavior> copy = m_behavior->clone();
    if (!copy)
    {
        const behavior& logic = *m_behavior;
        LOG_WRN("runtime::behavior_component: behaviour type '%s' does not implement clone(); left off the clone",
                typeid(logic).name());
        return std::nullopt;
    }
    return behavior_component{std::move(copy)};
}
