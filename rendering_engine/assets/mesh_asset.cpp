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

#include <rendering_engine/assets/mesh_asset.hpp>

#include <cstring>

#include <rendering_engine/assets/asset_device.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine
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

    mesh_asset::~mesh_asset()
    {
        // Free in the reverse of the create order used by the cache, matching
        // the premade renderables' teardown. The device outlives the cache, so
        // it is always installed here.
        auto& gpu = asset_device();
        if (index_buffer.valid())
        {
            gpu.destroy(index_buffer);
        }
        if (vertex_buffer.valid())
        {
            gpu.destroy(vertex_buffer);
        }
    }
} // namespace rendering_engine
