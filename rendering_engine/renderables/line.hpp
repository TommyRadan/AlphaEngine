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

    // A line (connected strip or independent segments). It
    // owns a list of vertex positions (with optional per-vertex colours)
    // and submits a single @ref draw_item against a line-topology
    // material (@ref line_material), so the scene pass rasterizes the
    // vertices as connected or independent line segments. The line width
    // is fixed at one pixel; tint lives on the material, geometry and the
    // model transform live here.
    //
    // The backend bakes line-list topology into the pipeline, so a
    // @c strip line is expanded into an index buffer of segment pairs at
    // @ref upload time; a @c segments line draws its vertices directly,
    // two per segment.
    struct line : public renderable
    {
        // The material is non-owning; it is typically a
        // @ref line_material (its pipeline must bake line topology)
        // created by @ref rendering_engine::context and shared by every
        // line that draws under it.
        explicit line(material* mat);
        ~line() override;

        rendering_engine::util::transform transform;

        // Select how vertices are joined. Takes effect on the next
        // @ref upload; defaults to @c line_mode::strip.
        void set_mode(line_mode mode);

        // Set the line vertices; every vertex defaults to white and is
        // tinted by the material colour. Replaces any previous data.
        // Call @ref upload afterwards to push it to the GPU.
        void set_positions(const std::vector<core::math::vec3>& positions);

        // Set positions with a matching per-vertex colour list. The two
        // vectors must be the same length; a size mismatch logs and the
        // call is ignored. Call @ref upload afterwards.
        void set_positions(const std::vector<core::math::vec3>& positions, const std::vector<core::math::vec3>& colors);

        // Upload the staged line data into a GPU vertex buffer (and, in
        // @c strip mode, the matching segment index buffer). Safe to
        // call again after a @ref set_positions to re-upload; the
        // previous buffers are released first.
        void upload() final;

        void collect_draw_items(std::vector<draw_item>& out) final;

    private:
        material* m_material{nullptr};
        line_mode m_mode{line_mode::strip};
        std::vector<vertex_position_color> m_vertices;

        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_index_buffer{};
        gpu::buffer m_draw_ubo{};
        gpu::bind_group m_draw_bind_group{};

        size_t m_vertex_count{0};
        size_t m_index_count{0};
        uint32_t m_vertex_stride{0};
    };
} // namespace rendering_engine
