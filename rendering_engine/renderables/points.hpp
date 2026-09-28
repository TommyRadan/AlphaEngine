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

    // A point cloud. It owns a list of point positions (with optional
    // per-point colours) and describes a single non-indexed draw against a
    // point-list material (@ref points_material), so the scene pass
    // rasterizes one point sprite per vertex. Size, tint and the optional
    // sprite live on the material, the geometry here, and the placement on
    // the proxy of whoever owns the cloud. Every change uploads the points
    // again, as a private @ref mesh_asset the cloud alone draws.
    struct points : public mesh_source
    {
        // The material is non-owning; it is typically a
        // @ref points_material (its pipeline must bake point topology)
        // created by @ref rendering_engine::renderer and shared by every
        // point cloud that draws under it. The points are uploaded to
        // @p device.
        points(gpu::device& device, material* mat);

        // Set the cloud positions and upload them; every point defaults to
        // white and is tinted by the material colour. Replaces any previous
        // data.
        void set_positions(const std::vector<core::math::vec3>& positions);

        // Set positions with a matching per-point colour list and upload
        // them. The two vectors must be the same length; a size mismatch
        // logs and the call is ignored.
        void set_positions(const std::vector<core::math::vec3>& positions, const std::vector<core::math::vec3>& colors);

        // The uploaded points and their box (none before any point is set);
        // the format is not checked against the material.
        mesh_description describe() const override;

        // None: a point cloud has no box a collider would fit.
        std::optional<core::math::aabb> local_bounds() const override;

    private:
        // Uploads @ref m_vertices as the drawn geometry.
        void upload();

        // The device the points are uploaded to; it outlives the cloud.
        gpu::device* m_device{nullptr};
        std::vector<assets::vertex_position_color> m_vertices;

        // Object-space box over the points at the last upload.
        std::optional<core::math::aabb> m_bounds;
    };
} // namespace rendering_engine
