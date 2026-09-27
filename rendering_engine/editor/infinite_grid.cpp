// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/editor/infinite_grid.hpp>

#include <array>
#include <cstdint>

#include <core/math/math.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/grid_material.hpp>
#include <rendering_engine/renderables/draw_item.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/engine.hpp>

namespace rendering_engine::editor
{
    infinite_grid::infinite_grid(float fade_distance)
        : helper("Grid (infinite)", helper_layer::scene),
          m_material(runtime::current_engine().renderer->create_grid_material(fade_distance))
    {
        upload();
    }

    infinite_grid::~infinite_grid()
    {
        auto& gpu = *runtime::current_engine().gpu;
        if (m_vertex_buffer.valid())
        {
            gpu.destroy(m_vertex_buffer);
            m_vertex_buffer = {};
        }
        m_material.reset();
    }

    void infinite_grid::upload()
    {
        auto& gpu = *runtime::current_engine().gpu;

        // A single fullscreen triangle in clip space; the grid material's
        // vertex shader unprojects these corners to reconstruct the view
        // rays, so no transform is applied to the positions themselves.
        const std::array<core::math::vec3, 3> vertices{core::math::vec3{-1.0f, -1.0f, 0.0f},
                                                       core::math::vec3{3.0f, -1.0f, 0.0f},
                                                       core::math::vec3{-1.0f, 3.0f, 0.0f}};

        gpu::buffer_descriptor vertex_descriptor{};
        vertex_descriptor.size = vertices.size() * sizeof(core::math::vec3);
        vertex_descriptor.usage = gpu::buffer_usage_vertex;
        vertex_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vertex_descriptor.initial_data = vertices.data();
        m_vertex_buffer = gpu.create_buffer(vertex_descriptor);
    }

    void infinite_grid::collect_draw_items(std::vector<draw_item>& out)
    {
        if (!visible || m_material == nullptr || !m_vertex_buffer.valid())
        {
            return;
        }

        draw_item item{};
        item.mat = m_material.get();
        // Per-draw block (the identity model of the origin grid, see
        // per_draw_ubo.hpp); the shader references the model matrix when
        // reconstructing depth. The grid never moves, so the block is
        // built once and then only pushed per frame.
        m_per_draw.bind(m_transform, item);
        item.vertex_buffer = m_vertex_buffer;
        item.vertex_stride = sizeof(core::math::vec3);
        item.vertex_count = 3;
        out.push_back(item);
    }
} // namespace rendering_engine::editor
