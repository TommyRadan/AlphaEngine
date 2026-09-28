// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/mesh_source.hpp>

#include <utility>

#include <rendering_engine/resources/mesh_asset.hpp>

namespace rendering_engine
{
    mesh_source::mesh_source(material* mat, const char* name) noexcept : m_material{mat}, m_name{name} {}

    mesh_description mesh_source::describe() const
    {
        mesh_description description{};
        description.mesh = m_mesh;
        description.mat = m_material;
        if (m_mesh != nullptr)
        {
            description.bounds = m_mesh->bounds;
        }
        description.layer_mask = m_layer_mask;
        description.name = m_name;
        return description;
    }

    std::optional<core::math::aabb> mesh_source::local_bounds() const
    {
        if (m_mesh == nullptr)
        {
            return std::nullopt;
        }
        return m_mesh->bounds;
    }

    void mesh_source::write_instances(render_world& world, mesh_proxy_handle proxy)
    {
        (void)world;
        (void)proxy;
    }

    void mesh_source::set_layer_mask(uint32_t mask) noexcept
    {
        if (mask != m_layer_mask)
        {
            m_layer_mask = mask;
            changed();
        }
    }

    void mesh_source::set_mesh(std::shared_ptr<mesh_asset> mesh) noexcept
    {
        m_mesh = std::move(mesh);
        changed();
    }

    void mesh_source::changed() noexcept
    {
        ++m_revision;
    }
} // namespace rendering_engine
