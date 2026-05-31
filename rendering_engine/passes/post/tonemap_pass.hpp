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

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>

namespace rendering_engine
{
    /**
     * @brief Tonemap operator the @ref tonemap_pass applies to the
     *        HDR scene colour, the analogue of @c THREE.ToneMapping.
     *
     * The enumerator values are the contract with the fragment
     * shader's @c Tonemap UBO — they are uploaded verbatim and
     * branched on at draw time, so they must not be reordered.
     */
    enum class tonemap_operator : int
    {
        /// No curve: the exposed colour is only clamped and gamma
        /// encoded (THREE.NoToneMapping / LinearToneMapping).
        none = 0,
        /// Reinhard's x / (1 + x) shoulder (THREE.ReinhardToneMapping).
        reinhard = 1,
        /// Krzysztof Narkowicz's ACES filmic approximation
        /// (THREE.ACESFilmicToneMapping). The default.
        aces = 2,
    };

    /**
     * @brief Maps the HDR scene-colour target into LDR for the
     *        swapchain via a selectable tonemap curve and gamma encode.
     *
     * Reads the rgba16f scene target produced by @ref scene_pass,
     * applies @c exposure as a pre-curve scale, runs the operator
     * selected via @ref set_operator (ACES filmic by default,
     * Reinhard, or a clamp-only linear path), and gamma-2.2 encodes
     * the result before writing to the off-screen LDR target. Without
     * this pass HDR luminance > 1.0 saturates to white as soon as it
     * hits the 8-bit backbuffer.
     *
     * It resolves into @ref frame_context::ldr_color_target rather
     * than straight to the swapchain so the trailing @ref fxaa_pass
     * can sample the tonemapped result as a shader input (the
     * swapchain is not sampleable). Slots into the post chain between
     * @ref scene_pass and @ref fxaa_pass. Pipeline state mirrors any
     * other fullscreen-
     * triangle post pass (depth off, blend off, no culling, no
     * vertex buffers); the shared
     * @ref fullscreen_triangle_vertex_shader emits the geometry
     * from @c gl_VertexID.
     *
     * @c exposure and the operator default to 1.0 / @ref
     * tonemap_operator::aces and are baked into the @c Tonemap UBO at
     * construction. Both are live-tunable: @ref set_exposure and
     * @ref set_operator rewrite the UBO immediately (the change lands
     * on the next recorded frame), the entry point a future
     * auto-exposure stage will drive the exposure through.
     */
    struct tonemap_pass : pass
    {
        explicit tonemap_pass(gpu::texture input_color);
        ~tonemap_pass() override;

        tonemap_pass(const tonemap_pass&) = delete;
        tonemap_pass& operator=(const tonemap_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        /// @brief Sets the pre-curve exposure scale; rewrites the UBO.
        void set_exposure(float exposure);
        /// @brief The current pre-curve exposure scale.
        float exposure() const
        {
            return m_exposure;
        }

        /// @brief Selects the tonemap curve; rewrites the UBO.
        void set_operator(tonemap_operator op);
        /// @brief The currently selected tonemap curve.
        tonemap_operator operator_kind() const
        {
            return m_operator;
        }

    private:
        // Repacks the { exposure, operator } pair and writes it to the
        // Tonemap UBO; called by the setters whenever a value changes.
        void upload_uniforms();

        // CPU-side mirror of the std140 @c Tonemap UBO: the float
        // exposure scale and the int operator selector.
        float m_exposure{1.0f};
        tonemap_operator m_operator{tonemap_operator::aces};

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_fragment_shader{};
        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_tonemap_ubo{};
        gpu::bind_group_layout m_input_layout{};
        gpu::bind_group m_input_bind_group{};
        gpu::pipeline m_pipeline{};
    };
} // namespace rendering_engine
