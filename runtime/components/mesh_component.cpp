// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/components/mesh_component.hpp>

#include <assets/mesh_data.hpp>
#include <core/log.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/renderer.hpp>
#include <rendering_engine/resources/mesh_asset.hpp>
#include <runtime/engine.hpp>
#include <runtime/node.hpp>

runtime::mesh_component::mesh_component(rendering_engine::material* material, const assets::mesh_data& mesh)
    : m_model{std::make_unique<rendering_engine::model>(*runtime::current_engine().gpu, material)}, m_material{material}
{
    m_model->upload_mesh(mesh);
    // Geometry with no complete record uploads nothing, so it has no box.
    if (mesh.vertex_stride != 0 && mesh.vertex_bytes.size() >= mesh.vertex_stride)
    {
        m_bounds = mesh.bounds.has_value() ? mesh.bounds : mesh.compute_bounds();
    }
}

runtime::mesh_component::mesh_component(rendering_engine::material* material,
                                        std::shared_ptr<rendering_engine::mesh_asset> mesh)
    : m_model{std::make_unique<rendering_engine::model>(*runtime::current_engine().gpu, material)},
      m_material{material}, m_mesh{mesh}
{
    m_model->set_mesh(std::move(mesh));
}

runtime::mesh_component::mesh_component(std::shared_ptr<rendering_engine::material> material,
                                        std::shared_ptr<rendering_engine::mesh_asset> mesh)
    : m_owned_material{std::move(material)}, m_material{m_owned_material.get()}, m_mesh{std::move(mesh)}
{
    if (m_material != nullptr)
    {
        m_model = std::make_unique<rendering_engine::model>(*runtime::current_engine().gpu, m_material);
        m_model->set_mesh(m_mesh);
    }
}

runtime::mesh_component runtime::mesh_component::clone() const
{
    if (!m_model)
    {
        return mesh_component{};
    }
    if (m_mesh == nullptr)
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
    if (!m_model)
    {
        return std::nullopt;
    }
    if (m_mesh != nullptr)
    {
        return m_mesh->bounds;
    }
    return m_bounds;
}

void runtime::mesh_component::on_attach(node& owner)
{
    if (!m_model)
    {
        return;
    }

    // Draw at the node's world transform: the model's local transform stays
    // identity and inherits the node pose through the transform parent chain.
    m_model->transform.set_parent(&owner.transform);
    register_model();
}

void runtime::mesh_component::on_destroy()
{
    unregister_model();
    // The owning node may outlive this component (remove_component, store
    // teardown); do not leave the model's transform pointing at it.
    if (m_model)
    {
        m_model->transform.set_parent(nullptr);
    }
}

void runtime::mesh_component::on_active_changed(node& owner, bool active)
{
    (void)owner;
    if (active)
    {
        register_model();
    }
    else
    {
        unregister_model();
    }
}

void runtime::mesh_component::register_model()
{
    if (m_model && !m_registered)
    {
        runtime::current_engine().renderer->register_scene_renderable(m_model.get());
        m_registered = true;
    }
}

void runtime::mesh_component::unregister_model()
{
    if (m_model && m_registered)
    {
        runtime::current_engine().renderer->unregister_scene_renderable(m_model.get());
        m_registered = false;
    }
}
