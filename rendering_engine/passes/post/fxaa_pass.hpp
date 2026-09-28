// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/frame_resources.hpp>
#include <rendering_engine/passes/pass.hpp>

namespace rendering_engine
{
    /**
     * @brief Fast approximate anti-aliasing on the tonemapped LDR image.
     *
     * The cheapest post-process AA: a single fullscreen pass that finds
     * luminance edges in the already-shaded LDR image and blends along
     * them, smoothing the jaggies the scene pass leaves behind without
     * any MSAA sample storage. It is the last link in the post chain,
     * running after @ref tonemap_pass on the LDR intermediate target and
     * writing the result straight to the view's output (the swapchain, or
     * a render texture) the @ref ui_pass then composites on top of.
     *
     * FXAA wants a perceptual (non-linear) image and computes its edge
     * luma from the colour directly, so it slots in *after* tonemap's
     * ACES + gamma encode rather than on the linear HDR target — sampling
     * the gamma-encoded LDR is exactly the input Timothy Lottes' original
     * shader assumes. The implementation is the canonical compact FXAA:
     * a 3x3 luma neighbourhood picks the edge direction, then two pairs
     * of bilinear taps along that direction produce the blended colour,
     * falling back to the tighter blend when the wider one drifts outside
     * the local luma range.
     *
     * Pipeline state mirrors every other fullscreen-triangle post pass
     * (depth off, blend off, no culling, single vec2 vertex attribute);
     * the shared @ref fullscreen_triangle_vertex_shader emits the
     * geometry. The reciprocal frame size (1/width, 1/height) the edge
     * search steps by is baked into a small UBO per view from the view's
     * size, mirroring the way @ref tonemap_pass captures its exposure, and
     * rewritten when the view is resized. A zero step collapses every tap
     * onto the centre texel so the pass becomes a straight copy — the
     * trick @ref frame_context::post's @c fxaa.enabled uses at runtime:
     * this pass always stays in the chain (it is what writes the output),
     * so disabling it rewrites the UBO with a zero step rather than
     * skipping the draw, and @ref prepare only pays for that rewrite when
     * the flag actually changed.
     *
     * The image it samples is not a constructor input: each frame it
     * takes @ref frame_resources::taa_resolve while that is published
     * (temporal AA on) and @ref frame_resources::ldr_color otherwise, and
     * binds an input bind group built against that handle. It writes the
     * result into the view's rectangle of its output
     * (@ref frame_resources::output: the swapchain or a render texture),
     * clearing the whole output first when the view is the first of the
     * frame to draw into it.
     * The TAA resolve alternates between two ping-pong targets from frame
     * to frame, so each view keeps its groups in a two-entry cache keyed by
     * handle: a handle seen before is rebound without work, a new one (the
     * first frame, a resize that recreated a target) replaces the entry
     * it displaces — which is how a resize reaches this pass.
     */
    struct fxaa_pass : pass
    {
        explicit fxaa_pass(gpu::device& device);
        ~fxaa_pass() override;

        fxaa_pass(const fxaa_pass&) = delete;
        fxaa_pass& operator=(const fxaa_pass&) = delete;

        // Picks and (on a miss) builds the view's input bind group for this
        // frame's image, and rewrites the view's edge step when the view's
        // size or the enabled flag changed.
        void prepare(const frame_context& ctx) override;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return builtin_passes::fxaa;
        }

        // The tonemapped LDR target is the input the pass falls back to;
        // the TAA resolve replaces it while one is published.
        void declare_io(pass_io_builder& io) const override
        {
            io.read(frame_resources::ldr_color);
            io.read_optional(frame_resources::taa_resolve);
            io.write(frame_resources::output);
        }

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

        // One cached input: the texture and the bind group built against
        // it plus the rcp_frame UBO.
        struct bound_input
        {
            gpu::texture texture{};
            gpu::bind_group bind_group{};
        };

        // What the pass keeps per view: the rcp_frame UBO and the edge step
        // it holds (the view's size, or 0 x 0 while FXAA is disabled), and
        // two cached inputs — the two halves of the TAA ping-pong, or just
        // the LDR target — with the slot the next miss is built into.
        // Entries are invalid until the view's first prepare() builds one.
        struct view_data final : pass_view_state
        {
            // Bakes the step for @p width x @p height into a new UBO.
            view_data(gpu::device& device, uint32_t width, uint32_t height);
            ~view_data() override;

            view_data(const view_data&) = delete;
            view_data& operator=(const view_data&) = delete;

            gpu::device* device{nullptr};
            gpu::buffer rcp_frame_ubo{};
            uint32_t baked_width{0};
            uint32_t baked_height{0};
            std::array<bound_input, 2> inputs{};
            size_t next_input_slot{0};
        };

        // The bind group for @p input_color in @p view: the cached one when
        // the handle was bound before, otherwise a new group built into the
        // slot the cache rotates to (releasing whatever it held).
        gpu::bind_group bind_group_for(view_data& view, gpu::texture input_color);

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_fragment_shader{};
        gpu::buffer m_vertex_buffer{};
        gpu::bind_group_layout m_input_layout{};
        gpu::pipeline m_pipeline{};

        // The cached group this frame draws with and the output it draws
        // into, picked by prepare().
        gpu::bind_group m_input_bind_group{};
        view_output m_output{};
    };
} // namespace rendering_engine
