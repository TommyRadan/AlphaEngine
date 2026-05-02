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

#include <rendering_engine/renderers/basic_renderer.hpp>

#include <string>

#include <control/engine.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace
{
    const std::string vertex_shader = R"vs(
        #version 330

        layout(location=0) in vec3 position;

        uniform mat4 modelMatrix;
        uniform mat4 viewMatrix;
        uniform mat4 projectionMatrix;

        void main()
        {
            mat4 MVP = projectionMatrix * viewMatrix * modelMatrix;
            gl_Position = MVP * vec4(position, 1.0);
        }
)vs";

    // Fragment shader for the 3D scene pass. Outputs a flat white
    // colour; the historical `color` uniform was declared but never
    // read, so it is gone.
    const std::string fragment_shader = R"fs(
        #version 330

        out vec4 fragColor;

        void main()
        {
            fragColor = vec4(1.0, 1.0, 1.0, 1.0);
        }
)fs";
} // namespace

namespace rendering_engine::renderers
{
    basic_renderer::basic_renderer()
    {
        gpu::vertex_buffer_layout vertex_layout{};
        // Stride = 0 — per-renderable strides are passed at
        // @c set_vertex_buffer time. The shader only reads
        // position, so attribute offsets stay at 0 across every
        // renderable that fronts this pipeline.
        vertex_layout.stride = 0;
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});

        gpu::bind_group_layout_descriptor frame_layout{};
        frame_layout.entries.push_back({0, gpu::binding_kind::mat4_value, "viewMatrix"});
        frame_layout.entries.push_back({1, gpu::binding_kind::mat4_value, "projectionMatrix"});

        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back({0, gpu::binding_kind::mat4_value, "modelMatrix"});

        gpu::depth_state depth{};
        depth.test_enabled = true;
        depth.write_enabled = true;
        depth.compare = gpu::compare_function::less;

        gpu::blend_state blend{};
        blend.enabled = true;
        blend.src = gpu::blend_factor::src_alpha;
        blend.dst = gpu::blend_factor::one_minus_src_alpha;

        gpu::rasterizer_state rasterizer{};
        rasterizer.cull = gpu::cull_mode::back;
        rasterizer.front = gpu::front_face::counter_clockwise;

        construct_pipeline(
            vertex_shader, fragment_shader, vertex_layout, draw_layout, &frame_layout, depth, blend, rasterizer);

        // Pre-allocate the per-frame bind group; entries are
        // refilled each frame via @c update_bind_group.
        gpu::bind_group_descriptor frame_bind_group_descriptor{};
        frame_bind_group_descriptor.layout = m_frame_layout;
        gpu::binding_value view_slot{};
        view_slot.binding = 0;
        view_slot.kind = gpu::binding_kind::mat4_value;
        gpu::binding_value projection_slot{};
        projection_slot.binding = 1;
        projection_slot.kind = gpu::binding_kind::mat4_value;
        frame_bind_group_descriptor.entries.push_back(view_slot);
        frame_bind_group_descriptor.entries.push_back(projection_slot);
        m_frame_bind_group = control::current_engine().gpu->create_bind_group(frame_bind_group_descriptor);
    }

    basic_renderer::~basic_renderer()
    {
        if (m_frame_bind_group.valid())
        {
            control::current_engine().gpu->destroy(m_frame_bind_group);
            m_frame_bind_group = {};
        }
    }

    void basic_renderer::begin(gpu::render_pass_encoder& encoder)
    {
        renderer::begin(encoder);

        auto* current_camera = rendering_engine::camera::get_current_camera();

        std::vector<gpu::binding_value> entries;
        entries.reserve(2);

        gpu::binding_value view{};
        view.binding = 0;
        view.kind = gpu::binding_kind::mat4_value;
        view.mat4_value = current_camera != nullptr ? current_camera->get_view_matrix() : infrastructure::math::mat4{};
        entries.push_back(view);

        gpu::binding_value projection{};
        projection.binding = 1;
        projection.kind = gpu::binding_kind::mat4_value;
        projection.mat4_value =
            current_camera != nullptr ? current_camera->get_projection_matrix() : infrastructure::math::mat4{};
        entries.push_back(projection);

        auto& gpu = *control::current_engine().gpu;
        gpu.update_bind_group(m_frame_bind_group, entries);
        encoder.set_bind_group(0, m_frame_bind_group);
    }
} // namespace rendering_engine::renderers
