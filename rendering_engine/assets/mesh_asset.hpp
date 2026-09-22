/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file mesh_asset.hpp
 * @brief Shared GPU geometry: a vertex (and optional index) buffer pair owned
 *        through a reference-counted asset handle.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>
#include <vector>

#include <core/math/aabb.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/mesh/vertex.hpp>

namespace rendering_engine
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
     * @brief CPU-side geometry handed to @ref asset_cache::get_or_create_mesh.
     *
     * Layout-agnostic: vertices are stored as raw interleaved bytes plus a
     * @ref vertex_stride, so a cached mesh is not tied to one vertex format
     * (position+uv+normal, +tangent, or a custom importer's record all work).
     * @ref format names the record layout when it is one of the engine's
     * vertex structs so renderables can check it against the material that
     * draws it; raw importer records leave it @c custom. The builder fills
     * this and the cache uploads it once. @ref indices may be left empty for
     * non-indexed geometry, in which case the resulting @ref mesh_asset
     * carries only a vertex buffer.
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
        // the cache derives them via @ref compute_bounds, which assumes a
        // @c vec3 position at offset 0 of every record — true of every
        // named format. A @c custom record that does not lead with its
        // position must fill this in (an importer already knows its
        // extents) or the cached box, and any culling based on it, is
        // wrong.
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

    /**
     * @brief A vertex/index buffer pair uploaded to the GPU exactly once and
     *        shared between every renderable that references the same key.
     *
     * Produced by @ref asset_cache::get_or_create_mesh and handed out as a
     * @c std::shared_ptr. The destructor releases both GPU buffers, so the
     * geometry lives exactly as long as the last live handle. This is what lets
     * many instances of identical procedural geometry (the cube lattice, the
     * premade sphere/box renderables) share one upload rather than each
     * uploading its own copy.
     *
     * Non-copyable and non-movable: each GPU buffer has a single owner and is
     * freed exactly once in the destructor.
     */
    struct mesh_asset
    {
        mesh_asset() = default;
        ~mesh_asset();

        mesh_asset(const mesh_asset&) = delete;
        mesh_asset& operator=(const mesh_asset&) = delete;
        mesh_asset(mesh_asset&&) = delete;
        mesh_asset& operator=(mesh_asset&&) = delete;

        gpu::buffer vertex_buffer{};
        gpu::buffer index_buffer{};
        uint32_t vertex_count{0};
        uint32_t index_count{0};

        // Bytes per vertex in @ref vertex_buffer; carried so consumers set their
        // draw-item vertex stride without assuming a fixed vertex format.
        uint32_t vertex_stride{0};

        // Record layout of @ref vertex_buffer, copied from the
        // @ref mesh_data that built it. Renderables compare it against the
        // material's @c required_vertex_format before emitting a draw so a
        // pipeline never fetches attributes the record does not carry.
        vertex_format format{vertex_format::custom};

        // Object-space bounds of @ref vertex_buffer: the builder-supplied
        // @ref mesh_data::bounds when present, otherwise derived from the
        // positions at upload. Renderables transform it by their world
        // matrix to answer @ref renderable::world_bounds, so the passes can
        // frustum-cull them. A zero box for empty geometry (which draws
        // nothing anyway).
        core::math::aabb bounds{};
    };
} // namespace rendering_engine
