// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <optional>
#include <vector>

#include <assets/vertex.hpp>
#include <core/math/aabb.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/renderables/mesh_source.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    struct material;

    // How successive vertices are joined into segments. @c strip joins
    // every vertex to the next (a connected
    // polyline); @c segments treats vertices as independent pairs,
    // so vertices 0-1 form one segment,
    // 2-3 the next, and so on.
    enum class line_mode
    {
        strip,
        segments,
    };

    // A line (connected strip or independent segments). It owns a list of
    // vertex positions (with optional per-vertex colours) and describes a
    // single draw against a line-topology material (@ref line_material), so
    // the scene pass rasterizes the vertices as connected or independent line
    // segments. The line width is fixed at one pixel; tint lives on the
    // material, the geometry here, and the placement on the proxy of whoever
    // owns the line.
    //
    // The backend bakes line-list topology into the pipeline, so a @c strip
    // line is expanded into an index buffer of segment pairs when it is
    // uploaded; a @c segments line draws its vertices directly, two per
    // segment. Every change uploads the geometry again, as a private
    // @ref mesh_asset the line alone draws.
    struct line : public mesh_source
    {
        // The material is non-owning; it is typically a
        // @ref line_material (its pipeline must bake line topology)
        // created by @ref rendering_engine::renderer and shared by every
        // line that draws under it. The geometry is uploaded to @p device.
        line(gpu::device& device, material* mat);

        // Select how vertices are joined; defaults to @c line_mode::strip.
        // Uploads the vertices again when there are any.
        void set_mode(line_mode mode);

        // Set the line vertices and upload them; every vertex defaults to
        // white and is tinted by the material colour. Replaces any previous
        // data.
        void set_positions(const std::vector<core::math::vec3>& positions);

        // Set positions with a matching per-vertex colour list and upload
        // them. The two vectors must be the same length; a size mismatch
        // logs and the call is ignored.
        void set_positions(const std::vector<core::math::vec3>& positions, const std::vector<core::math::vec3>& colors);

        // The uploaded geometry, its box over every vertex (none before any
        // vertex is set) and, for segments, the even vertex count drawn; the
        // format is not checked against the material.
        mesh_description describe() const override;

        // None: a line has no box a collider would fit.
        std::optional<core::math::aabb> local_bounds() const override;

    private:
        // Uploads @ref m_vertices (and a strip's segment indices) as the
        // drawn geometry.
        void upload();

        // The device the geometry is uploaded to; it outlives the line.
        gpu::device* m_device{nullptr};
        line_mode m_mode{line_mode::strip};
        std::vector<assets::vertex_position_color> m_vertices;

        // Object-space box over the vertices at the last upload.
        std::optional<core::math::aabb> m_bounds;
    };
} // namespace rendering_engine
