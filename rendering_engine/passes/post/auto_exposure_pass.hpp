// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <array>
#include <cstdint>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>

namespace rendering_engine
{
    /**
     * @brief Eye adaptation: meters the HDR image on the GPU and hands
     *        @ref tonemap_pass an exposure through a persistent 1x1 texture.
     *
     * Runs after @ref bloom_pass (so it meters exactly the image tonemap
     * maps) and before @ref tonemap_pass, entirely in fullscreen-triangle
     * draws on tiny targets, with no CPU readback and no compute:
     *
     *  1. Luminance: the HDR image (@ref frame_context::hdr_color_texture)
     *     into a 64 x 64 target of log2 luminance, each texel metering a
     *     4 x 4 grid of bilinear taps over its share of the frame. A tap
     *     with next to no light (below 2^-10: the cleared background
     *     where nothing was drawn) is left out of the average rather than
     *     counted as very dark, so an empty backdrop does not push the
     *     exposure to its limit; the texel keeps the weighted log sum and
     *     the weight side by side.
     *  2. Reduction: 64 → 16 → 4, each step averaging 4 x 4 texels with
     *     four bilinear taps, so the last level holds sixteen partial
     *     averages of the whole frame.
     *  3. Adaptation: averages that level to the frame's log2 geometric
     *     mean luminance, converts it to EV100, clamps it to
     *     [@ref auto_exposure_settings::min_ev, @c max_ev] and eases the
     *     adapted value toward it from last frame's (at @c speed_up while
     *     the scene brightens, @c speed_down while it darkens, scaled by
     *     the frame delta), writing the adapted EV100 and the log2
     *     exposure (middle grey at the adapted brightness, plus
     *     @c compensation stops) into a 1 x 1 @c rgba16f target.
     *  4. Store: copies that texel into a second 1 x 1 target, the history
     *     the next frame's adaptation reads, so the result tonemap samples
     *     (@ref exposure_texture) keeps one stable handle.
     *
     * The histogram route (a compute pass binning luminance) meters more
     * robustly but would add the engine's first mid-frame compute work and
     * its synchronisation to the post chain; the log-average reduction
     * reuses the render-pass machinery every post pass already relies on.
     *
     * Every target is a fixed size, independent of the drawable, so
     * @ref resize has nothing to rebuild: the adapted value (a property of
     * the scene's brightness, not of the window) carries over a resize
     * untouched, and only the luminance stage's input bind group is rebuilt
     * when the frame context hands it a new HDR handle.
     *
     * The first metered frame — at startup, or after auto exposure is
     * re-enabled — snaps to the target instead of easing in from an
     * undefined history, so nothing flashes. A frame without a camera
     * meters nothing and keeps the last adapted value (the scene pass
     * clears the HDR image to black then, which would otherwise drag the
     * exposure to its limit). While disabled the pass records nothing and
     * tonemap applies @ref post_settings::exposure; @ref produces_exposure
     * is the rule @ref renderer::render publishes
     * @ref frame_context::exposure_texture by.
     */
    struct auto_exposure_pass : pass
    {
        auto_exposure_pass();
        ~auto_exposure_pass() override;

        auto_exposure_pass(const auto_exposure_pass&) = delete;
        auto_exposure_pass& operator=(const auto_exposure_pass&) = delete;

        // Decides whether the frame meters, writes the adaptation params
        // and rebinds the HDR input when its handle changed.
        void prepare(const frame_context& ctx) override;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "auto_exposure";
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read("scene_color");
            io.write("exposure");
        }

        /**
         * @brief Whether @ref exposure_texture holds a valid exposure once
         *        this frame's @ref record has run: auto exposure is
         *        enabled and either a camera is metered this frame or an
         *        earlier frame already adapted. Asked by the renderer
         *        before the passes prepare.
         */
        bool produces_exposure(const frame_context& ctx) const;

        /// The 1x1 adaptation result tonemap samples (see
        /// @ref frame_context::exposure_texture). Stable for the pass's
        /// lifetime.
        gpu::texture exposure_texture() const;

    private:
        // One level of the log2 luminance reduction: its target, and the
        // bind group that samples the level before it (the HDR input for
        // level 0, which prepare() rebuilds on a new handle instead).
        struct reduction_level
        {
            gpu::render_target target{};
            gpu::texture texture{};
            gpu::bind_group source_bind_group{};
        };

        // A 1x1-or-larger rgba16f target without depth.
        static gpu::render_target create_target(uint32_t size);

        // A {texture @0} bind group on @ref m_texture_layout.
        gpu::bind_group create_texture_bind_group(gpu::texture texture) const;

        // Packs this frame's adaptation params (the settings, whether to
        // snap, the frame delta) into @ref m_params_ubo.
        void upload_params(const frame_context& ctx, bool reset);

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_luminance_shader{};
        gpu::shader_module m_downsample_shader{};
        gpu::shader_module m_adapt_shader{};
        gpu::shader_module m_store_shader{};
        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_params_ubo{};

        // {texture @0} for the luminance, reduction and store stages;
        // {luminance @0, history @1, params @2} for the adaptation.
        gpu::bind_group_layout m_texture_layout{};
        gpu::bind_group_layout m_adapt_layout{};

        gpu::pipeline m_luminance_pipeline{};
        gpu::pipeline m_downsample_pipeline{};
        gpu::pipeline m_adapt_pipeline{};
        gpu::pipeline m_store_pipeline{};

        // 64 x 64, 16 x 16 and 4 x 4.
        std::array<reduction_level, 3> m_levels{};

        // The adaptation result tonemap samples, and the copy of it the
        // next frame's adaptation reads as history.
        gpu::render_target m_adapted_target{};
        gpu::texture m_adapted_texture{};
        gpu::render_target m_history_target{};
        gpu::texture m_history_texture{};
        gpu::bind_group m_adapt_bind_group{};
        gpu::bind_group m_store_bind_group{};

        // The HDR texture level 0's bind group was built against; invalid
        // until the first metered frame builds it.
        gpu::texture m_bound_input{};

        // Whether the history holds a real adapted value: false before
        // the first metered frame and again whenever auto exposure is
        // disabled, so the next metered frame snaps to its target. Set
        // by prepare() for the frame record() meters.
        bool m_has_history{false};

        // Whether this frame's record() meters (enabled and a camera),
        // decided by prepare().
        bool m_meters{false};
    };
} // namespace rendering_engine
