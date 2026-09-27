// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <assets/mesh_data.hpp>

#include <cstring>

#include <core/math/vec3.hpp>

namespace assets
{
    std::optional<core::math::aabb>
    compute_position_bounds(const void* vertices, std::size_t byte_count, uint32_t vertex_stride)
    {
        constexpr std::size_t position_bytes = sizeof(core::math::vec3);
        if (vertices == nullptr || vertex_stride < position_bytes || byte_count < vertex_stride)
        {
            return std::nullopt;
        }

        // memcpy rather than a reinterpret_cast: the record bytes carry no
        // alignment guarantee (an importer may hand over packed data).
        const auto* bytes = static_cast<const std::byte*>(vertices);
        const std::size_t count = byte_count / vertex_stride;

        core::math::vec3 position;
        std::memcpy(&position, bytes, position_bytes);
        core::math::aabb bounds{position, position};
        for (std::size_t i = 1; i < count; ++i)
        {
            std::memcpy(&position, bytes + i * vertex_stride, position_bytes);
            bounds = core::math::merge(bounds, position);
        }
        return bounds;
    }

    std::optional<core::math::aabb> mesh_data::compute_bounds() const
    {
        return compute_position_bounds(vertex_bytes.data(), vertex_bytes.size(), vertex_stride);
    }
} // namespace assets
