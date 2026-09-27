// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file mesh_asset.hpp
 * @brief Shared GPU geometry: a vertex (and optional index) buffer pair owned
 *        through a reference-counted asset handle.
 */

#pragma once

#include <cstdint>
#include <string>

#include <assets/vertex.hpp>
#include <core/math/aabb.hpp>
#include <rendering_engine/gpu/handle.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    /**
     * @brief A vertex/index buffer pair uploaded to the GPU exactly once and
     *        shared between every renderable that references the same key.
     *
     * Produced by @ref asset_cache::get_or_create_mesh from an
     * @c assets::mesh_data and handed out as a @c std::shared_ptr. The
     * destructor releases both GPU buffers on the device the cache created
     * them on, so the geometry lives exactly as long as the last live
     * handle. This is what lets many instances of identical procedural
     * geometry (the cube lattice, the premade sphere/box renderables) share
     * one upload rather than each uploading its own copy.
     *
     * Non-copyable and non-movable: each GPU buffer has a single owner and is
     * freed exactly once in the destructor.
     */
    struct mesh_asset
    {
        /** @brief An empty asset whose buffers, once set, belong to @p device. */
        explicit mesh_asset(gpu::device& device);
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
        // @c assets::mesh_data that built it. Renderables compare it against
        // the material's @c required_vertex_format before emitting a draw so
        // a pipeline never fetches attributes the record does not carry.
        assets::vertex_format format{assets::vertex_format::custom};

        // Object-space bounds of @ref vertex_buffer, kept on the CPU: the
        // builder-supplied @c assets::mesh_data::bounds when present,
        // otherwise derived from the positions at upload. Renderables
        // transform it by their world matrix to answer
        // @ref renderable::world_bounds, so the passes can frustum-cull
        // them, and world systems (physics collider fitting) read it
        // without touching the GPU buffers. A zero box for empty geometry
        // (which draws nothing anyway).
        core::math::aabb bounds{};

        // The structural key @ref asset_cache::get_or_create_mesh cached the
        // asset under, or empty for one made outside the cache. It names the
        // geometry beyond this process: a scene file stores it as the
        // mesh's reference and resolves it through the cache on load.
        std::string key;

    private:
        gpu::device* m_device;
    };
} // namespace rendering_engine
