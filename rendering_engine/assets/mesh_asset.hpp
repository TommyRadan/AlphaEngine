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
#include <type_traits>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/mesh/vertex.hpp>

namespace rendering_engine
{
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
    };
} // namespace rendering_engine
