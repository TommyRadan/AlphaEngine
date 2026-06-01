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

#include <rendering_engine/materials/grid_material.hpp>

#include <string>

#include <rendering_engine/gpu/types.hpp>

namespace
{
    constexpr uint32_t draw_model_binding = 1;

    // Fullscreen triangle: the vertex positions arrive already in clip
    // space (xy in {-1, 3}), so the vertex stage only has to unproject
    // the near / far plane points back into world space for the fragment
    // ray-march. binding 0 (camera) lives in the scene per-frame set, the
    // per-draw model matrix at binding 1 places the ground plane.
    const std::string vertex_shader = R"vs(
        #version 450

        layout(location = 0) in vec3 position;

        layout(set = 0, binding = 0, std140) uniform PerFrame
        {
            mat4 viewMatrix;
            mat4 projectionMatrix;
        } u_frame;

        layout(set = 1, binding = 1, std140) uniform PerDraw
        {
            mat4 modelMatrix;
        } u_draw;

        layout(location = 0) out vec3 nearPoint;
        layout(location = 1) out vec3 farPoint;
        layout(location = 2) out vec3 cameraPoint;

        vec3 unproject(vec2 ndc, float z, mat4 inverseViewProj)
        {
            vec4 world = inverseViewProj * vec4(ndc, z, 1.0);
            return world.xyz / world.w;
        }

        void main()
        {
            mat4 viewProj = u_frame.projectionMatrix * u_frame.viewMatrix;
            mat4 inverseViewProj = inverse(viewProj);

            vec2 ndc = position.xy;
            // GL-convention clip space: near plane at z = -1, far at z = 1.
            nearPoint = unproject(ndc, -1.0, inverseViewProj);
            farPoint = unproject(ndc, 1.0, inverseViewProj);
            cameraPoint = inverse(u_frame.viewMatrix)[3].xyz;

            gl_Position = vec4(ndc, 0.0, 1.0);
        }
)vs";

    // The fragment shader does the real work: intersect the per-pixel
    // view ray with the z = 0 ground plane, draw two grid scales plus the
    // X/Y world axes with derivative-based anti-aliasing, fade with
    // distance, and write the reconstructed depth so the scene occludes
    // the grid. @c {{FADE}} is substituted with the fade radius.
    const std::string fragment_shader_template = R"fs(
        #version 450

        layout(location = 0) in vec3 nearPoint;
        layout(location = 1) in vec3 farPoint;
        layout(location = 2) in vec3 cameraPoint;

        layout(set = 0, binding = 0, std140) uniform PerFrame
        {
            mat4 viewMatrix;
            mat4 projectionMatrix;
        } u_frame;

        layout(set = 1, binding = 1, std140) uniform PerDraw
        {
            mat4 modelMatrix;
        } u_draw;

        layout(location = 0) out vec4 fragColor;

        // Coverage of the nearest grid line at the given spacing, with
        // screen-space-derivative anti-aliasing. 1.0 on a line, 0.0 in a
        // cell.
        float gridCoverage(vec2 p, float spacing)
        {
            vec2 coord = p / spacing;
            vec2 derivative = fwidth(coord);
            vec2 distance = abs(fract(coord - 0.5) - 0.5) / max(derivative, vec2(1e-8));
            float line = min(distance.x, distance.y);
            return 1.0 - min(line, 1.0);
        }

        void main()
        {
            vec3 rayDirection = farPoint - nearPoint;

            // Intersect the ray with the Z-up ground plane (z = 0). t
            // outside (0, 1) means the plane is behind the camera or past
            // the far plane, so there is nothing to draw.
            float t = -nearPoint.z / rayDirection.z;
            if (t <= 0.0 || t >= 1.0)
            {
                discard;
            }

            vec3 world = nearPoint + t * rayDirection;

            // Reconstructed depth (references the per-draw model matrix so
            // the binding stays live; identity for the origin grid). Map
            // the GL-convention clip z in [-1, 1] to the [0, 1] window
            // depth range used by both backends.
            vec4 clip = u_frame.projectionMatrix * u_frame.viewMatrix * u_draw.modelMatrix * vec4(world, 1.0);
            gl_FragDepth = 0.5 * (clip.z / clip.w) + 0.5;

            vec2 plane = world.xy;
            float minor = gridCoverage(plane, 1.0);
            float major = gridCoverage(plane, 10.0);

            vec3 minorColor = vec3(0.32);
            vec3 majorColor = vec3(0.55);
            vec3 color = mix(minorColor, majorColor, major);
            float alpha = max(minor * 0.55, major);

            // Coloured world axes: the Y axis (x = 0) green, the X axis
            // (y = 0) red, each one derivative-width wide.
            vec2 axisWidth = fwidth(plane);
            if (abs(world.x) < axisWidth.x)
            {
                color = vec3(0.25, 0.85, 0.35);
                alpha = max(alpha, 1.0);
            }
            if (abs(world.y) < axisWidth.y)
            {
                color = vec3(0.9, 0.3, 0.35);
                alpha = max(alpha, 1.0);
            }

            // Fade out with distance from the camera so the grid dissolves
            // into the horizon rather than aliasing into a solid mass.
            float fade = 1.0 - clamp(length(world - cameraPoint) / {{FADE}}, 0.0, 1.0);
            alpha *= fade * fade;

            if (alpha <= 0.001)
            {
                discard;
            }
            fragColor = vec4(color, alpha);
        }
)fs";

    std::string build_fragment_shader(float fade_distance)
    {
        std::string source = fragment_shader_template;
        const std::string token = "{{FADE}}";
        const std::string value = std::to_string(fade_distance);
        for (size_t pos = source.find(token); pos != std::string::npos; pos = source.find(token))
        {
            source.replace(pos, token.size(), value);
        }
        return source;
    }
} // namespace

namespace rendering_engine
{
    grid_material::grid_material(gpu::bind_group_layout frame_layout, float fade_distance)
    {
        gpu::vertex_buffer_layout vertex_layout{};
        // Stride supplied per draw by the renderable; one vec3 position.
        vertex_layout.stride = 0;
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});

        // Per-draw layout (slot 1): the model matrix UBO at binding 1,
        // matching the renderable's bind group.
        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back({draw_model_binding, gpu::binding_kind::uniform_buffer});

        // Transparent so the grid fades; depth tested but not written, so
        // scene geometry occludes the grid while the grid never clobbers
        // the depth buffer. Double-sided so the fullscreen triangle is
        // never culled.
        material_params params{};
        params.transparent = true;
        params.blending = blend_mode::normal;
        params.double_sided = true;
        params.depth_test = true;
        params.depth_write = false;

        construct_pipeline(vertex_shader,
                           build_fragment_shader(fade_distance),
                           vertex_layout,
                           draw_layout,
                           frame_layout,
                           params,
                           gpu::bind_group_layout_descriptor{},
                           gpu::primitive_topology::triangles);
    }

    grid_material::~grid_material() = default;
} // namespace rendering_engine
