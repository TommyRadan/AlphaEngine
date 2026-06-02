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

#include <rendering_engine/passes/post/bloom_pass.hpp>

#include <algorithm>
#include <array>
#include <string>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/passes/post/fullscreen_triangle.hpp>
#include <runtime/engine.hpp>

namespace
{
    // Number of mips in the blur pyramid. Five levels spread the glow
    // across roughly a thirtieth of the screen at the coarsest mip while
    // keeping the target count modest. The pyramid stops growing once a
    // dimension clamps to one texel, so smaller windows simply reuse the
    // floor resolution for their tail levels.
    constexpr uint32_t bloom_mip_count = 5;

    // Bright-pass cutoff in HDR luminance. The scene target is rgba16f
    // and lights routinely push lit surfaces past 1.0, so a threshold of
    // 1.0 blooms only genuinely over-bright pixels and leaves the diffuse
    // midtones untouched.
    constexpr float bloom_threshold = 1.0f;

    // Soft-knee width as a fraction of the threshold. Gives the bright
    // pass a quadratic roll-in around the cutoff instead of a hard step,
    // so a surface drifting through the threshold fades in rather than
    // popping.
    constexpr float bloom_soft_knee = 0.5f;

    // Overall glow strength. Distributed across the mips by the
    // per-level weights below; the levels' weights sum to this value so
    // it reads as the total energy the bloom adds back into the scene.
    constexpr float bloom_strength = 0.6f;

    // Tiny epsilon guarding the divisions in the bright-pass knee and the
    // luminance normalise.
    constexpr float bloom_epsilon = 1.0e-4f;

    // std140 rounds every one-to-four-float block up to a 16-byte
    // allocation, so each baked params UBO is a single vec4.
    constexpr size_t params_ubo_size = 16;

    // Bright-pass: extract pixels above the threshold with a soft knee.
    // @c params packs (threshold, knee, 2*knee, 1/(4*knee)) so the knee
    // curve costs no per-pixel divides. The original colour is preserved
    // and merely scaled by its contribution, so a bloomed highlight keeps
    // its hue.
    const std::string threshold_shader = R"fs(
        #version 450

        layout(location = 0) in vec2 texCoord;
        layout(location = 0) out vec4 fragColor;

        layout(set = 0, binding = 0) uniform sampler2D sceneColor;

        layout(set = 0, binding = 1, std140) uniform Threshold
        {
            vec4 params; // x threshold, y knee, z 2*knee, w 1/(4*knee)
        } u_threshold;

        void main()
        {
            vec3 color = texture(sceneColor, texCoord).rgb;
            float brightness = max(color.r, max(color.g, color.b));

            float threshold = u_threshold.params.x;
            float knee = u_threshold.params.y;

            // Quadratic soft knee around the threshold, then the hard
            // cutoff above it; the larger of the two wins.
            float soft = clamp(brightness - threshold + knee, 0.0, u_threshold.params.z);
            soft = soft * soft * u_threshold.params.w;
            float contribution = max(soft, brightness - threshold);
            contribution /= max(brightness, 0.0001);

            fragColor = vec4(color * contribution, 1.0);
        }
)fs";

    // Separable Gaussian. @c offset.xy is the per-tap texel step along the
    // blur axis (horizontal or vertical), baked from the source texture's
    // texel size. Nine taps (centre + four mirrored pairs) with the
    // standard sigma-2 weights. clamp_edge sampling on the render-target
    // textures keeps the kernel well-defined at the borders.
    const std::string blur_shader = R"fs(
        #version 450

        layout(location = 0) in vec2 texCoord;
        layout(location = 0) out vec4 fragColor;

        layout(set = 0, binding = 0) uniform sampler2D src;

        layout(set = 0, binding = 1, std140) uniform Blur
        {
            vec4 offset; // xy texel step per tap, zw unused
        } u_blur;

        void main()
        {
            const float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
            vec2 step = u_blur.offset.xy;

            vec3 result = texture(src, texCoord).rgb * weight[0];
            for (int i = 1; i < 5; ++i)
            {
                result += texture(src, texCoord + step * float(i)).rgb * weight[i];
                result += texture(src, texCoord - step * float(i)).rgb * weight[i];
            }

            fragColor = vec4(result, 1.0);
        }
)fs";

    // Composite: scale a blurred mip by its weight and emit it. The
    // pipeline blends additively (one, one) so each level accumulates on
    // top of the scene already in the target.
    const std::string composite_shader = R"fs(
        #version 450

        layout(location = 0) in vec2 texCoord;
        layout(location = 0) out vec4 fragColor;

        layout(set = 0, binding = 0) uniform sampler2D bloomColor;

        layout(set = 0, binding = 1, std140) uniform Composite
        {
            vec4 params; // x weight, yzw unused
        } u_composite;

        void main()
        {
            vec3 color = texture(bloomColor, texCoord).rgb;
            fragColor = vec4(color * u_composite.params.x, 1.0);
        }
)fs";
} // namespace

namespace rendering_engine
{
    bloom_pass::bloom_pass(gpu::texture scene_color, uint32_t width, uint32_t height)
    {
        auto& gpu = *runtime::current_engine().gpu;

        // Degenerate backbuffer (no settings, zero-sized window): leave
        // the pass disabled so the scene target flows straight through to
        // tonemap untouched.
        if (width == 0 || height == 0)
        {
            return;
        }

        // -- Shaders --------------------------------------------------
        gpu::shader_module_descriptor vs_descriptor{};
        vs_descriptor.stage = gpu::shader_stage::vertex;
        vs_descriptor.spirv = gpu::compile_glsl_to_spirv(fullscreen_triangle_vertex_shader, gpu::shader_stage::vertex);
        m_vertex_shader = gpu.create_shader_module(vs_descriptor);

        gpu::shader_module_descriptor threshold_descriptor{};
        threshold_descriptor.stage = gpu::shader_stage::fragment;
        threshold_descriptor.spirv = gpu::compile_glsl_to_spirv(threshold_shader, gpu::shader_stage::fragment);
        m_threshold_shader = gpu.create_shader_module(threshold_descriptor);

        gpu::shader_module_descriptor blur_descriptor{};
        blur_descriptor.stage = gpu::shader_stage::fragment;
        blur_descriptor.spirv = gpu::compile_glsl_to_spirv(blur_shader, gpu::shader_stage::fragment);
        m_blur_shader = gpu.create_shader_module(blur_descriptor);

        gpu::shader_module_descriptor composite_descriptor{};
        composite_descriptor.stage = gpu::shader_stage::fragment;
        composite_descriptor.spirv = gpu::compile_glsl_to_spirv(composite_shader, gpu::shader_stage::fragment);
        m_composite_shader = gpu.create_shader_module(composite_descriptor);

        // -- Fullscreen-triangle vertex buffer ------------------------
        gpu::buffer_descriptor vb_descriptor{};
        vb_descriptor.size = fullscreen_triangle_vertices.size() * sizeof(float);
        vb_descriptor.usage = gpu::buffer_usage_vertex;
        vb_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vb_descriptor.initial_data = fullscreen_triangle_vertices.data();
        m_vertex_buffer = gpu.create_buffer(vb_descriptor);

        // -- Shared {texture, ubo} layout -----------------------------
        gpu::bind_group_layout_descriptor io_layout{};
        io_layout.entries.push_back({0, gpu::binding_kind::texture});
        io_layout.entries.push_back({1, gpu::binding_kind::uniform_buffer});
        m_io_layout = gpu.create_bind_group_layout(io_layout);

        // -- Fixed-function state shared by the fullscreen stages ------
        gpu::vertex_buffer_layout vertex_layout{};
        vertex_layout.stride = sizeof(float) * 2;
        vertex_layout.attributes.push_back({0, 2, gpu::scalar_type::float32, 0});

        gpu::depth_state depth{};
        depth.test_enabled = false;
        depth.write_enabled = false;
        depth.compare = gpu::compare_function::always;

        gpu::rasterizer_state rasterizer{};
        rasterizer.cull = gpu::cull_mode::none;
        rasterizer.front = gpu::front_face::counter_clockwise;
        rasterizer.polygon = gpu::polygon_mode::fill;

        // Threshold and blur write into a freshly cleared target, so no
        // blending; the composite accumulates each mip on top of the
        // scene already in the target, so it blends additively.
        gpu::blend_state opaque_blend{};
        opaque_blend.enabled = false;

        gpu::blend_state additive_blend{};
        additive_blend.enabled = true;
        additive_blend.src = gpu::blend_factor::one;
        additive_blend.dst = gpu::blend_factor::one;
        additive_blend.op = gpu::blend_op::add;

        auto make_pipeline = [&](gpu::shader_module fragment, const gpu::blend_state& blend)
        {
            gpu::pipeline_descriptor descriptor{};
            descriptor.vertex_shader = m_vertex_shader;
            descriptor.fragment_shader = fragment;
            descriptor.vertex_buffers.push_back(vertex_layout);
            descriptor.depth = depth;
            descriptor.blend = blend;
            descriptor.rasterizer = rasterizer;
            descriptor.bind_group_layouts.push_back(m_io_layout);
            return gpu.create_pipeline(descriptor);
        };

        m_threshold_pipeline = make_pipeline(m_threshold_shader, opaque_blend);
        m_blur_pipeline = make_pipeline(m_blur_shader, opaque_blend);
        m_composite_pipeline = make_pipeline(m_composite_shader, additive_blend);

        // Helpers shared by every stage's resource setup.
        auto make_target = [&](uint32_t w, uint32_t h)
        {
            gpu::render_target_descriptor descriptor{};
            descriptor.color_format = gpu::texture_format::rgba16_float;
            descriptor.width = w;
            descriptor.height = h;
            descriptor.with_depth = false;
            return gpu.create_render_target(descriptor);
        };

        auto make_params_ubo = [&](const std::array<float, 4>& values)
        {
            gpu::buffer_descriptor descriptor{};
            descriptor.size = params_ubo_size;
            descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
            descriptor.hint = gpu::buffer_usage_hint::static_data;
            descriptor.initial_data = values.data();
            return gpu.create_buffer(descriptor);
        };

        auto make_bind_group = [&](gpu::texture input, gpu::buffer ubo)
        {
            gpu::bind_group_descriptor descriptor{};
            descriptor.layout = m_io_layout;

            gpu::binding_value texture_slot{};
            texture_slot.binding = 0;
            texture_slot.kind = gpu::binding_kind::texture;
            texture_slot.texture_value = input;
            descriptor.entries.push_back(texture_slot);

            gpu::binding_value ubo_slot{};
            ubo_slot.binding = 1;
            ubo_slot.kind = gpu::binding_kind::uniform_buffer;
            ubo_slot.buffer_value = ubo;
            descriptor.entries.push_back(ubo_slot);

            return gpu.create_bind_group(descriptor);
        };

        // -- Bright pass: half-resolution threshold output ------------
        const uint32_t base_width = std::max(1u, width / 2);
        const uint32_t base_height = std::max(1u, height / 2);
        m_bright_target = make_target(base_width, base_height);
        m_bright_texture = gpu.render_target_color_texture(m_bright_target);

        const float knee = bloom_threshold * bloom_soft_knee;
        m_threshold_ubo = make_params_ubo({bloom_threshold, knee, 2.0f * knee, 1.0f / (4.0f * knee + bloom_epsilon)});
        m_threshold_bind_group = make_bind_group(scene_color, m_threshold_ubo);

        // -- Blur pyramid ---------------------------------------------
        // Per-level weights fall off linearly toward the coarser mips and
        // are normalised so they sum to bloom_strength: the wide, blurry
        // low-frequency levels contribute less than the tight bright core.
        float weight_total = 0.0f;
        for (uint32_t i = 0; i < bloom_mip_count; ++i)
        {
            weight_total += static_cast<float>(bloom_mip_count - i);
        }

        m_levels.reserve(bloom_mip_count);
        for (uint32_t i = 0; i < bloom_mip_count; ++i)
        {
            bloom_level level{};
            level.width = std::max(1u, base_width >> i);
            level.height = std::max(1u, base_height >> i);

            level.horizontal_target = make_target(level.width, level.height);
            level.horizontal_texture = gpu.render_target_color_texture(level.horizontal_target);
            level.vertical_target = make_target(level.width, level.height);
            level.vertical_texture = gpu.render_target_color_texture(level.vertical_target);

            // Horizontal blur samples the previous level's fully blurred
            // mip (or the bright pass for level 0); the per-tap step is
            // that source's texel width, so a smaller target downsamples
            // as it blurs.
            const gpu::texture horizontal_input = (i == 0) ? m_bright_texture : m_levels[i - 1].vertical_texture;
            const uint32_t source_width = (i == 0) ? base_width : m_levels[i - 1].width;
            level.blur_horizontal_ubo = make_params_ubo({1.0f / static_cast<float>(source_width), 0.0f, 0.0f, 0.0f});
            level.blur_horizontal_bind_group = make_bind_group(horizontal_input, level.blur_horizontal_ubo);

            // Vertical blur samples the horizontal result at this level's
            // own resolution.
            level.blur_vertical_ubo = make_params_ubo({0.0f, 1.0f / static_cast<float>(level.height), 0.0f, 0.0f});
            level.blur_vertical_bind_group = make_bind_group(level.horizontal_texture, level.blur_vertical_ubo);

            const float weight = bloom_strength * static_cast<float>(bloom_mip_count - i) / weight_total;
            level.weight_ubo = make_params_ubo({weight, 0.0f, 0.0f, 0.0f});
            level.composite_bind_group = make_bind_group(level.vertical_texture, level.weight_ubo);

            m_levels.push_back(level);
        }

        m_enabled = true;
    }

    bloom_pass::~bloom_pass()
    {
        auto& gpu = *runtime::current_engine().gpu;

        // Bind groups first, then the buffers / targets they reference.
        for (auto& level : m_levels)
        {
            if (level.composite_bind_group.valid())
            {
                gpu.destroy(level.composite_bind_group);
            }
            if (level.blur_vertical_bind_group.valid())
            {
                gpu.destroy(level.blur_vertical_bind_group);
            }
            if (level.blur_horizontal_bind_group.valid())
            {
                gpu.destroy(level.blur_horizontal_bind_group);
            }
            if (level.weight_ubo.valid())
            {
                gpu.destroy(level.weight_ubo);
            }
            if (level.blur_vertical_ubo.valid())
            {
                gpu.destroy(level.blur_vertical_ubo);
            }
            if (level.blur_horizontal_ubo.valid())
            {
                gpu.destroy(level.blur_horizontal_ubo);
            }
            // Each render target owns its colour attachment, so destroying
            // the target releases the texture too.
            if (level.vertical_target.valid())
            {
                gpu.destroy(level.vertical_target);
            }
            if (level.horizontal_target.valid())
            {
                gpu.destroy(level.horizontal_target);
            }
        }
        m_levels.clear();

        if (m_threshold_bind_group.valid())
        {
            gpu.destroy(m_threshold_bind_group);
            m_threshold_bind_group = {};
        }
        if (m_threshold_ubo.valid())
        {
            gpu.destroy(m_threshold_ubo);
            m_threshold_ubo = {};
        }
        if (m_bright_target.valid())
        {
            gpu.destroy(m_bright_target);
            m_bright_target = {};
            m_bright_texture = {};
        }
        if (m_composite_pipeline.valid())
        {
            gpu.destroy(m_composite_pipeline);
            m_composite_pipeline = {};
        }
        if (m_blur_pipeline.valid())
        {
            gpu.destroy(m_blur_pipeline);
            m_blur_pipeline = {};
        }
        if (m_threshold_pipeline.valid())
        {
            gpu.destroy(m_threshold_pipeline);
            m_threshold_pipeline = {};
        }
        if (m_io_layout.valid())
        {
            gpu.destroy(m_io_layout);
            m_io_layout = {};
        }
        if (m_vertex_buffer.valid())
        {
            gpu.destroy(m_vertex_buffer);
            m_vertex_buffer = {};
        }
        if (m_composite_shader.valid())
        {
            gpu.destroy(m_composite_shader);
            m_composite_shader = {};
        }
        if (m_blur_shader.valid())
        {
            gpu.destroy(m_blur_shader);
            m_blur_shader = {};
        }
        if (m_threshold_shader.valid())
        {
            gpu.destroy(m_threshold_shader);
            m_threshold_shader = {};
        }
        if (m_vertex_shader.valid())
        {
            gpu.destroy(m_vertex_shader);
            m_vertex_shader = {};
        }
    }

    void bloom_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        if (!m_enabled)
        {
            return;
        }

        // Draws a single fullscreen triangle into the currently open pass.
        auto draw_fullscreen =
            [&](gpu::render_pass_encoder* pass_encoder, gpu::pipeline pipeline, gpu::bind_group bind_group)
        {
            pass_encoder->set_pipeline(pipeline);
            pass_encoder->set_bind_group(0, bind_group);
            pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
            pass_encoder->draw(3, 0);
        };

        // 1. Bright pass: scene colour → half-res threshold target.
        {
            gpu::render_pass_descriptor descriptor{};
            descriptor.target = m_bright_target;
            descriptor.color.load = gpu::load_op::clear;
            descriptor.color.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            descriptor.use_depth = false;

            auto pass_encoder = encoder.begin_render_pass(descriptor);
            draw_fullscreen(pass_encoder.get(), m_threshold_pipeline, m_threshold_bind_group);
            pass_encoder->end();
        }

        // 2. Separable Gaussian down the pyramid: horizontal then vertical
        //    per level. begin_render_pass defaults the viewport to each
        //    target's own extent, so the smaller mips downsample for free.
        for (const auto& level : m_levels)
        {
            {
                gpu::render_pass_descriptor descriptor{};
                descriptor.target = level.horizontal_target;
                descriptor.color.load = gpu::load_op::clear;
                descriptor.color.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
                descriptor.use_depth = false;

                auto pass_encoder = encoder.begin_render_pass(descriptor);
                draw_fullscreen(pass_encoder.get(), m_blur_pipeline, level.blur_horizontal_bind_group);
                pass_encoder->end();
            }
            {
                gpu::render_pass_descriptor descriptor{};
                descriptor.target = level.vertical_target;
                descriptor.color.load = gpu::load_op::clear;
                descriptor.color.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
                descriptor.use_depth = false;

                auto pass_encoder = encoder.begin_render_pass(descriptor);
                draw_fullscreen(pass_encoder.get(), m_blur_pipeline, level.blur_vertical_bind_group);
                pass_encoder->end();
            }
        }

        // 3. Additively composite every blurred mip back into the HDR
        //    scene target. Loading (not clearing) preserves the scene the
        //    tonemap pass will read; linear sampling upscales each mip to
        //    full resolution as it is composited.
        {
            gpu::render_pass_descriptor descriptor{};
            descriptor.target = ctx.scene_color_target;
            descriptor.color.load = gpu::load_op::load;
            descriptor.use_depth = false;

            auto pass_encoder = encoder.begin_render_pass(descriptor);
            pass_encoder->set_pipeline(m_composite_pipeline);
            for (const auto& level : m_levels)
            {
                pass_encoder->set_bind_group(0, level.composite_bind_group);
                pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
                pass_encoder->draw(3, 0);
            }
            pass_encoder->end();
        }
    }
} // namespace rendering_engine
