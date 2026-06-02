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

#pragma once

#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <rendering_engine/util/transform.hpp>

namespace rendering_engine
{
    struct material;

    // A point cloud. It owns a list
    // of point positions (with optional per-point colours) and submits
    // a single non-indexed @ref draw_item against a point-list material
    // (@ref points_material), so the scene pass rasterizes one GL point
    // sprite per vertex. Size, tint and the optional sprite live on the
    // material; the geometry and the model transform live here.
    struct points : public renderable
    {
        // The material is non-owning; it is typically a
        // @ref points_material (its pipeline must bake point topology)
        // created by @ref rendering_engine::context and shared by every
        // point cloud that draws under it.
        explicit points(material* mat);
        ~points() override;

        rendering_engine::util::transform transform;

        // Set the cloud positions; every point defaults to white and is
        // tinted by the material colour. Replaces any previous data.
        // Call @ref upload afterwards to push it to the GPU.
        void set_positions(const std::vector<core::math::vec3>& positions);

        // Set positions with a matching per-point colour list. The two
        // vectors must be the same length; a size mismatch logs and the
        // call is ignored. Call @ref upload afterwards.
        void set_positions(const std::vector<core::math::vec3>& positions, const std::vector<core::math::vec3>& colors);

        // Upload the staged point data into a GPU vertex buffer. Safe to
        // call again after a @ref set_positions to re-upload; the
        // previous buffer is released first.
        void upload() final;

        void collect_draw_items(std::vector<draw_item>& out) final;

    private:
        material* m_material{nullptr};
        std::vector<vertex_position_color> m_vertices;

        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_draw_ubo{};
        gpu::bind_group m_draw_bind_group{};

        size_t m_vertex_count{0};
        uint32_t m_vertex_stride{0};
    };
} // namespace rendering_engine
