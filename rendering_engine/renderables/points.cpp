// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/points.hpp>

#include <assets/mesh_data.hpp>
#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/resources/mesh_asset.hpp>

rendering_engine::points::points(gpu::device& device, material* mat) : mesh_source{mat, "points"}, m_device{&device} {}

void rendering_engine::points::set_positions(const std::vector<core::math::vec3>& positions)
{
    m_vertices.clear();
    m_vertices.reserve(positions.size());
    for (const auto& position : positions)
    {
        m_vertices.push_back({position, core::math::vec3{1.0f, 1.0f, 1.0f}});
    }
    upload();
}

void rendering_engine::points::set_positions(const std::vector<core::math::vec3>& positions,
                                             const std::vector<core::math::vec3>& colors)
{
    if (positions.size() != colors.size())
    {
        LOG_WRN("points::set_positions: positions (%zu) and colors (%zu) size mismatch; ignoring",
                positions.size(),
                colors.size());
        return;
    }

    m_vertices.clear();
    m_vertices.reserve(positions.size());
    for (size_t i = 0; i < positions.size(); ++i)
    {
        m_vertices.push_back({positions[i], colors[i]});
    }
    upload();
}

void rendering_engine::points::upload()
{
    // Box the points once per upload; an empty cloud is unbounded and draws
    // nothing.
    m_bounds = assets::compute_position_bounds(m_vertices.data(),
                                               m_vertices.size() * sizeof(assets::vertex_position_color),
                                               sizeof(assets::vertex_position_color));
    if (m_vertices.empty())
    {
        set_mesh(nullptr);
        return;
    }

    assets::mesh_data data = assets::mesh_data::from_vertices(m_vertices);
    data.bounds = m_bounds;
    set_mesh(upload_mesh(*m_device, data, data.format));
}

rendering_engine::mesh_description rendering_engine::points::describe() const
{
    mesh_description description = mesh_source::describe();
    description.bounds = m_bounds;
    description.check_format = false;
    return description;
}

std::optional<core::math::aabb> rendering_engine::points::local_bounds() const
{
    return std::nullopt;
}
