// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file mesh_data.hpp
 * @brief CPU-side geometry: interleaved vertex records of any layout, their
 *        optional indices and object-space bounds.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <assets/vertex.hpp>
#include <core/math/aabb.hpp>

namespace assets
{
    /**
     * @brief Object-space box enclosing the positions of an interleaved
     *        vertex array, reading a @c vec3 at offset 0 of every
     *        @p vertex_stride-byte record.
     *
     * Every named @ref vertex_format leads with its position, so this is
     * the bounds of any engine vertex struct. Returns @c std::nullopt when
     * @p byte_count holds no complete record or @p vertex_stride is too
     * short to carry a @c vec3, so a caller never boxes garbage.
     */
    std::optional<core::math::aabb>
    compute_position_bounds(const void* vertices, std::size_t byte_count, uint32_t vertex_stride);

    /**
     * @brief CPU-side geometry, as built by a procedural builder or an
     *        importer and uploaded by the renderer's asset cache.
     *
     * Layout-agnostic: vertices are stored as raw interleaved bytes plus a
     * @ref vertex_stride, so the geometry is not tied to one vertex format
     * (position+uv+normal, +tangent, or a custom importer's record all work).
     * @ref format names the record layout when it is one of the engine's
     * vertex structs so renderables can check it against the material that
     * draws it; raw importer records leave it @c custom. @ref indices may be
     * left empty for non-indexed geometry. @ref bounds, or the box
     * @ref compute_bounds derives, is the CPU-side extent world systems such
     * as physics size themselves to.
     */
    struct mesh_data
    {
        // Raw interleaved vertex data. Fill via @ref from_vertices from a typed
        // vertex container, or write directly from a mesh importer that already
        // has interleaved bytes.
        std::vector<std::byte> vertex_bytes;

        // Optional 32-bit indices; empty means non-indexed geometry.
        std::vector<uint32_t> indices;

        // Size of one vertex in @ref vertex_bytes, in bytes. Must be non-zero
        // for a valid mesh; @ref from_vertices sets it from @c sizeof(VertexT).
        uint32_t vertex_stride{0};

        // Record layout of @ref vertex_bytes. @ref from_vertices deduces it
        // from @c VertexT (one of the @c vertex_* structs, else @c custom); a
        // builder that writes raw bytes sets it by hand, or leaves @c custom
        // when the layout is not one the engine names. For a named format
        // @ref vertex_stride must equal @ref vertex_format_stride.
        vertex_format format{vertex_format::custom};

        // Optional object-space bounds supplied by the builder. When unset
        // they are derived via @ref compute_bounds, which assumes a
        // @c vec3 position at offset 0 of every record — true of every
        // named format. A @c custom record that does not lead with its
        // position must fill this in (an importer already knows its
        // extents) or the box, and any culling based on it, is wrong.
        std::optional<core::math::aabb> bounds;

        /**
         * @brief Bounds of the positions in @ref vertex_bytes, reading a
         *        @c vec3 at offset 0 of every @ref vertex_stride record.
         *
         * @c std::nullopt for empty geometry or a stride shorter than a
         * @c vec3; see @ref compute_position_bounds.
         */
        std::optional<core::math::aabb> compute_bounds() const;

        /**
         * @brief Builds @ref vertex_bytes from a typed, trivially-copyable
         *        vertex container.
         *
         * The vertex layout is whatever @c VertexT is, so the same cache holds
         * meshes of any format; @ref format is recorded via
         * @ref vertex_format_of so an engine vertex struct is named and any
         * other record is @c custom. @p idx is optional (empty leaves the mesh
         * non-indexed).
         */
        template<typename VertexT>
        static mesh_data from_vertices(const std::vector<VertexT>& verts, std::vector<uint32_t> idx = {})
        {
            static_assert(std::is_trivially_copyable_v<VertexT>, "mesh vertex type must be trivially copyable");

            mesh_data data;
            data.vertex_stride = static_cast<uint32_t>(sizeof(VertexT));
            data.format = vertex_format_of_v<VertexT>;
            data.vertex_bytes.resize(verts.size() * sizeof(VertexT));
            if (!verts.empty())
            {
                std::memcpy(data.vertex_bytes.data(), verts.data(), data.vertex_bytes.size());
            }
            data.indices = std::move(idx);
            return data;
        }
    };
} // namespace assets
