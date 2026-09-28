// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/components/renderable_component.hpp>

#include <core/log.hpp>
#include <rendering_engine/render_world.hpp>
#include <runtime/node.hpp>
#include <runtime/scene.hpp>

void runtime::renderable_component::on_attach(node& owner)
{
    if (!m_source)
    {
        return;
    }
    runtime::scene* scene = owner.scene();
    m_world = scene != nullptr ? scene->world() : nullptr;
    if (m_world == nullptr)
    {
        LOG_WRN("runtime::renderable_component::on_attach: node has no scene render_world; the mesh has no proxy");
        return;
    }
    create_proxy(owner);
}

void runtime::renderable_component::on_destroy()
{
    destroy_proxy();
    m_world = nullptr;
}

void runtime::renderable_component::on_active_changed(node& owner, bool active)
{
    if (active)
    {
        create_proxy(owner);
    }
    else
    {
        destroy_proxy();
    }
}

void runtime::renderable_component::extract(const node& owner)
{
    if (m_world == nullptr || !m_source || !m_proxy.valid())
    {
        return;
    }

    const uint64_t revision = m_source->revision();
    if (revision != m_described_revision)
    {
        m_world->set_mesh_source(m_proxy, m_source->describe());
        m_source->write_instances(*m_world, m_proxy);
        m_described_revision = revision;
    }

    const uint64_t version = owner.transform.get_world_version();
    if (version != m_placed_version)
    {
        m_world->set_mesh_world(m_proxy, owner.transform.get_world_matrix());
        m_placed_version = version;
    }
}

void runtime::renderable_component::create_proxy(const node& owner)
{
    if (m_world == nullptr || !m_source || m_proxy.valid())
    {
        return;
    }
    // The first extraction captures an instanced source's records into the
    // new proxy's empty snapshot.
    m_proxy = m_world->create_mesh(m_source->describe(), owner.transform.get_world_matrix());
    m_placed_version = owner.transform.get_world_version();
    m_described_revision = 0;
}

void runtime::renderable_component::destroy_proxy()
{
    if (m_world != nullptr && m_proxy.valid())
    {
        m_world->destroy_mesh(m_proxy);
    }
    m_proxy = {};
}
