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

#include <memory>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <rendering_engine/util/transform.hpp>

namespace rendering_engine
{
    struct material;
    struct mesh_asset;

    // Capsule aligned to +Y and centred at the origin: a straight
    // cylindrical body of height @c length (spanning [-length/2,
    // +length/2]) capped by two hemispheres of @c radius. The surface is
    // generated as a single seamless lat/lon grid — the top hemisphere,
    // the cylindrical body, and the bottom hemisphere share rings so
    // there are no cracks. Vertex format is position + uv + normal; UVs
    // wrap radially and run along the axis. Parameterised by
    // (radius, length, cap_segments, radial_segments).
    struct capsule : public renderable
    {
        explicit capsule(material* mat,
                         float radius = 0.5f,
                         float length = 1.0f,
                         unsigned int cap_segments = 8,
                         unsigned int radial_segments = 16);
        ~capsule() override;

        rendering_engine::util::transform transform;

        void upload() final;
        void collect_draw_items(std::vector<draw_item>& out) final;

        gpu::buffer get_vertex_buffer() const;
        gpu::buffer get_index_buffer() const;
        unsigned int get_index_count() const;

    private:
        material* m_material{nullptr};
        float m_radius;
        float m_length;
        unsigned int m_cap_segments;
        unsigned int m_radial_segments;
        unsigned int m_index_count{0};
        uint32_t m_vertex_stride{0};

        // Shared geometry from the asset cache, keyed by radius, length and
        // segment counts; freed when the last capsule referencing it is
        // destroyed.
        std::shared_ptr<mesh_asset> m_mesh;
        gpu::buffer m_draw_ubo{};
        gpu::bind_group m_draw_bind_group{};
    };
} // namespace rendering_engine
