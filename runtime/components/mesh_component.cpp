// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/components/mesh_component.hpp>

#include <utility>

#include <assets/mesh_data.hpp>
#include <core/log.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/mesh_proxy.hpp>
#include <rendering_engine/render_world.hpp>
#include <rendering_engine/resources/mesh_asset.hpp>
#include <runtime/engine.hpp>
#include <runtime/node.hpp>
#include <runtime/scene.hpp>

runtime::mesh_component::mesh_component(rendering_engine::material* material, const assets::mesh_data& mesh)
    : m_material{material}, m_private{true}, m_drawn{true}
{
    m_private_mesh = rendering_engine::upload_mesh(*runtime::current_engine().gpu, mesh, mesh.format);
    if (m_private_mesh == nullptr)
    {
        LOG_WRN("runtime::mesh_component: mesh has no vertices; nothing uploaded");
        return;
    }
    m_bounds = mesh.bounds.has_value() ? mesh.bounds : mesh.compute_bounds();
}

runtime::mesh_component::mesh_component(rendering_engine::material* material,
                                        std::shared_ptr<rendering_engine::mesh_asset> mesh)
    : m_material{material}, m_mesh{std::move(mesh)}, m_drawn{true}
{
}

runtime::mesh_component::mesh_component(std::shared_ptr<rendering_engine::material> material,
                                        std::shared_ptr<rendering_engine::mesh_asset> mesh)
    : m_owned_material{std::move(material)}, m_material{m_owned_material.get()}, m_mesh{std::move(mesh)},
      m_drawn{m_material != nullptr}
{
}

runtime::mesh_component runtime::mesh_component::clone() const
{
    if (!m_drawn)
    {
        return mesh_component{};
    }
    if (m_private)
    {
        LOG_WRN("runtime::mesh_component::clone: a privately uploaded mesh cannot be copied; the clone draws nothing");
        return mesh_component{};
    }
    mesh_component copy{m_material, m_mesh};
    copy.m_owned_material = m_owned_material;
    return copy;
}

std::optional<core::math::aabb> runtime::mesh_component::local_bounds() const
{
    if (!m_drawn)
    {
        return std::nullopt;
    }
    if (m_private)
    {
        return m_bounds;
    }
    if (m_mesh == nullptr)
    {
        return std::nullopt;
    }
    return m_mesh->bounds;
}

void runtime::mesh_component::set_joint_matrices(std::span<const core::math::mat4> matrices)
{
    m_joints.assign(matrices.begin(), matrices.end());
    ++m_joints_revision;
}

void runtime::mesh_component::on_attach(node& owner)
{
    if (!m_drawn)
    {
        return;
    }
    runtime::scene* scene = owner.scene();
    m_world = scene != nullptr ? scene->world() : nullptr;
    if (m_world == nullptr)
    {
        LOG_WRN("runtime::mesh_component::on_attach: node has no scene render_world; the mesh has no proxy");
        return;
    }
    create_proxy(owner);
}

void runtime::mesh_component::on_destroy()
{
    destroy_proxy();
    m_world = nullptr;
}

void runtime::mesh_component::on_active_changed(node& owner, bool active)
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

void runtime::mesh_component::extract(const node& owner)
{
    if (m_world == nullptr || !m_proxy.valid())
    {
        return;
    }

    if (m_joints_revision != m_written_joints_revision)
    {
        m_world->set_mesh_joints(m_proxy, m_joints);
        m_written_joints_revision = m_joints_revision;
    }

    const uint64_t version = owner.transform.get_world_version();
    if (version != m_placed_version)
    {
        m_world->set_mesh_world(m_proxy, owner.transform.get_world_matrix());
        m_placed_version = version;
    }
}

void runtime::mesh_component::create_proxy(const node& owner)
{
    if (m_world == nullptr || !m_drawn || m_proxy.valid())
    {
        return;
    }

    rendering_engine::mesh_description description{};
    description.mesh = m_private ? m_private_mesh : m_mesh;
    description.mat = m_material;
    if (description.mesh != nullptr)
    {
        description.bounds = m_private ? m_bounds : std::optional<core::math::aabb>{description.mesh->bounds};
    }
    description.name = "mesh_component";

    m_proxy = m_world->create_mesh(description, owner.transform.get_world_matrix());
    m_placed_version = owner.transform.get_world_version();
    // The first extraction hands a new proxy the current palette.
    m_written_joints_revision = 0;
}

void runtime::mesh_component::destroy_proxy()
{
    if (m_world != nullptr && m_proxy.valid())
    {
        m_world->destroy_mesh(m_proxy);
    }
    m_proxy = {};
}
