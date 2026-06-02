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

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>

namespace rendering_engine
{
    /**
     * @brief Adds an HDR bloom glow to the scene-colour target before
     *        tonemap.
     *
     * Runs entirely on the existing @c rgba16f scene target produced by
     * @ref scene_pass and slots into the post chain between it and
     * @ref tonemap_pass. The effect is the classic threshold →
     * downsample/blur → upsample/composite pipeline:
     *
     *  1. A bright-pass extracts pixels above @c bloom_threshold (with a
     *     soft knee) from the scene colour into a half-resolution target.
     *  2. A pyramid of progressively smaller mips is blurred with a
     *     separable Gaussian — a horizontal then a vertical pass per
     *     level — so each level both blurs and downsamples its input.
     *  3. Every blurred mip is additively composited back into the HDR
     *     scene target, the smaller (wider-spread) levels weighted lower.
     *     Linear sampling upscales each mip to full resolution as it is
     *     composited, so the downsample chain doubles as the upsample
     *     chain.
     *
     * Because the composite writes straight into the scene-colour target
     * the subsequent @ref tonemap_pass needs no changes: it maps the
     * bloomed HDR result to LDR exactly as before. Every pass uses the
     * shared fullscreen-triangle pattern (depth off, no culling, no
     * vertex buffers beyond the @ref fullscreen_triangle_vertices).
     *
     * Thresholds, blur offsets and composite weights are static — they
     * depend only on the target dimensions known at construction — so
     * they are baked into per-stage UBOs once, mirroring the way
     * @ref tonemap_pass captures its exposure. Live tuning is out of
     * scope.
     */
    struct bloom_pass : pass
    {
        // @p scene_color is the HDR scene-colour texture the bright-pass
        // samples; @p width / @p height are the backbuffer dimensions the
        // mip pyramid is sized against. The composite target is taken from
        // @ref frame_context::scene_color_target each frame.
        bloom_pass(gpu::texture scene_color, uint32_t width, uint32_t height);
        ~bloom_pass() override;

        bloom_pass(const bloom_pass&) = delete;
        bloom_pass& operator=(const bloom_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

    private:
        // One mip of the blur pyramid. @c horizontal holds the result of
        // the horizontal Gaussian (and the implicit downsample from the
        // previous level); @c vertical holds the fully blurred mip that
        // feeds both the next level and the composite.
        struct bloom_level
        {
            gpu::render_target horizontal_target{};
            gpu::texture horizontal_texture{};
            gpu::render_target vertical_target{};
            gpu::texture vertical_texture{};

            gpu::buffer blur_horizontal_ubo{};
            gpu::buffer blur_vertical_ubo{};
            gpu::buffer weight_ubo{};

            gpu::bind_group blur_horizontal_bind_group{};
            gpu::bind_group blur_vertical_bind_group{};
            gpu::bind_group composite_bind_group{};

            uint32_t width{0};
            uint32_t height{0};
        };

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_threshold_shader{};
        gpu::shader_module m_blur_shader{};
        gpu::shader_module m_composite_shader{};

        gpu::buffer m_vertex_buffer{};

        // One shared {texture @0, uniform_buffer @1} layout backs every
        // stage's pipeline and bind group — the bright-pass, both blur
        // directions and the composite all read a single input texture
        // plus a small params UBO.
        gpu::bind_group_layout m_io_layout{};

        gpu::pipeline m_threshold_pipeline{};
        gpu::pipeline m_blur_pipeline{};
        gpu::pipeline m_composite_pipeline{};

        // Half-resolution bright-pass output; also the input to mip 0.
        gpu::render_target m_bright_target{};
        gpu::texture m_bright_texture{};
        gpu::buffer m_threshold_ubo{};
        gpu::bind_group m_threshold_bind_group{};

        std::vector<bloom_level> m_levels;

        // False when the backbuffer dimensions are degenerate (no
        // settings, zero-sized window); record() then no-ops so the scene
        // target passes straight through to tonemap.
        bool m_enabled{false};
    };
} // namespace rendering_engine
