// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/line.hpp>

#include <cstdint>
#include <vector>

#include <assets/mesh_data.hpp>
#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/resources/mesh_asset.hpp>

rendering_engine::line::line(gpu::device& device, material* mat) : mesh_source{mat, "line"}, m_device{&device} {}

void rendering_engine::line::set_mode(line_mode mode)
{
    m_mode = mode;
    if (!m_vertices.empty())
    {
        upload();
    }
}

void rendering_engine::line::set_positions(const std::vector<core::math::vec3>& positions)
{
    m_vertices.clear();
    m_vertices.reserve(positions.size());
    for (const auto& position : positions)
    {
        m_vertices.push_back({position, core::math::vec3{1.0f, 1.0f, 1.0f}});
    }
    upload();
}

void rendering_engine::line::set_positions(const std::vector<core::math::vec3>& positions,
                                           const std::vector<core::math::vec3>& colors)
{
    if (positions.size() != colors.size())
    {
        LOG_WRN("line::set_positions: positions (%zu) and colors (%zu) size mismatch; ignoring",
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

void rendering_engine::line::upload()
{
    // Box the vertices once per upload; an empty line is unbounded and
    // draws nothing.
    m_bounds = assets::compute_position_bounds(m_vertices.data(),
                                               m_vertices.size() * sizeof(assets::vertex_position_color),
                                               sizeof(assets::vertex_position_color));
    if (m_vertices.empty())
    {
        set_mesh(nullptr);
        return;
    }

    std::vector<uint32_t> indices;
    if (m_mode == line_mode::strip && m_vertices.size() >= 2)
    {
        // A strip joins consecutive vertices, but the backend bakes
        // line-list topology, so expand the polyline into segment pairs:
        // vertex i pairs with i + 1 for every i in [0, count - 1). Fewer
        // than two vertices span no segment.
        indices.reserve((m_vertices.size() - 1) * 2);
        for (uint32_t i = 0; i + 1 < static_cast<uint32_t>(m_vertices.size()); ++i)
        {
            indices.push_back(i);
            indices.push_back(i + 1);
        }
    }

    assets::mesh_data data = assets::mesh_data::from_vertices(m_vertices, std::move(indices));
    data.bounds = m_bounds;
    set_mesh(upload_mesh(*m_device, data, data.format));
}

rendering_engine::mesh_description rendering_engine::line::describe() const
{
    mesh_description description = mesh_source::describe();
    description.bounds = m_bounds;
    description.check_format = false;
    if (mesh() != nullptr && !mesh()->index_buffer.valid())
    {
        // Segments draw the vertices directly, two per segment; an odd
        // trailing vertex has no partner, so it is dropped from the draw
        // (the buffer still holds it).
        description.vertex_count = mesh()->vertex_count & ~1u;
    }
    return description;
}

std::optional<core::math::aabb> rendering_engine::line::local_bounds() const
{
    return std::nullopt;
}
