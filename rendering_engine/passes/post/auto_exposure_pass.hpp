// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <array>
#include <cstdint>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/frame_resources.hpp>
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
     *  1. Luminance: the HDR image (@ref frame_resources::scene_color, as
     *     the passes before this one left it) into a 64 x 64 target of log2
     *     luminance, each texel metering a
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
     *     (@ref frame_resources::exposure) keeps one stable handle.
     *
     * The histogram route (a compute pass binning luminance) meters more
     * robustly but would add the engine's first mid-frame compute work and
     * its synchronisation to the post chain; the log-average reduction
     * reuses the render-pass machinery every post pass already relies on.
     *
     * Each view meters and adapts on its own (see @ref view_data), so two
     * views of differently lit places each settle on their own exposure.
     * Every target is a fixed size, independent of the view's, so a view's
     * resize has nothing to rebuild: the adapted value (a property of the
     * scene's brightness, not of the window) carries over a resize
     * untouched, and only the luminance stage's input bind group is rebuilt
     * when it looks up a new HDR handle.
     *
     * A view's first metered frame — when the view appears, or after auto
     * exposure is re-enabled — snaps to the target instead of easing in
     * from an
     * undefined history, so nothing flashes. A frame without a camera
     * meters nothing and keeps the last adapted value (the scene pass
     * clears the HDR image to black then, which would otherwise drag the
     * exposure to its limit). The pass publishes the result as
     * @ref frame_resources::exposure on every frame it holds a valid one:
     * auto exposure is enabled and either a camera is metered this frame
     * or an earlier frame already adapted. While disabled the pass records
     * and publishes nothing and tonemap applies @ref post_settings::exposure.
     */
    struct auto_exposure_pass : pass
    {
        explicit auto_exposure_pass(gpu::device& device);
        ~auto_exposure_pass() override;

        auto_exposure_pass(const auto_exposure_pass&) = delete;
        auto_exposure_pass& operator=(const auto_exposure_pass&) = delete;

        // Decides whether the frame meters, writes the adaptation params,
        // rebinds the HDR input when its handle changed and publishes the
        // adapted exposure when there is one.
        void prepare(const frame_context& ctx) override;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return builtin_passes::auto_exposure;
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read(frame_resources::scene_color);
            io.write(frame_resources::exposure);
        }

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

        // One level of the log2 luminance reduction: its target, and the
        // bind group that samples the level before it (the HDR input for
        // level 0, which prepare() rebuilds on a new handle instead).
        struct reduction_level
        {
            gpu::render_target target{};
            gpu::texture texture{};
            gpu::bind_group source_bind_group{};
        };

        // What the pass keeps per view: the adaptation params UBO, the
        // reduction levels (64 x 64, 16 x 16 and 4 x 4), the adaptation
        // result tonemap samples and the copy of it the next frame's
        // adaptation reads as history, with the bind groups over them, the
        // HDR texture level 0's bind group was built against (invalid until
        // the view's first metered frame builds it), and whether the
        // history holds a real adapted value: false before the view's first
        // metered frame and again whenever auto exposure is disabled, so
        // the next metered frame snaps to its target.
        struct view_data final : pass_view_state
        {
            explicit view_data(gpu::device& device) : device{&device} {}
            ~view_data() override;

            view_data(const view_data&) = delete;
            view_data& operator=(const view_data&) = delete;

            gpu::device* device{nullptr};
            gpu::buffer params_ubo{};
            std::array<reduction_level, 3> levels{};
            gpu::render_target adapted_target{};
            gpu::texture adapted_texture{};
            gpu::render_target history_target{};
            gpu::texture history_texture{};
            gpu::bind_group adapt_bind_group{};
            gpu::bind_group store_bind_group{};
            gpu::texture bound_input{};
            bool has_history{false};
        };

        // A 1x1-or-larger rgba16f target without depth.
        gpu::render_target create_target(uint32_t size) const;

        // A {texture @0} bind group on @ref m_texture_layout.
        gpu::bind_group create_texture_bind_group(gpu::texture texture) const;

        // Builds @p view's params UBO, targets and the bind groups between
        // them; level 0's input group waits for prepare().
        void build_view(view_data& view) const;

        // Packs this frame's adaptation params (the settings, whether to
        // snap, the frame delta) into @p view's params UBO.
        static void upload_params(const frame_context& ctx, const view_data& view, bool reset);

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_luminance_shader{};
        gpu::shader_module m_downsample_shader{};
        gpu::shader_module m_adapt_shader{};
        gpu::shader_module m_store_shader{};
        gpu::buffer m_vertex_buffer{};

        // {texture @0} for the luminance, reduction and store stages;
        // {luminance @0, history @1, params @2} for the adaptation.
        gpu::bind_group_layout m_texture_layout{};
        gpu::bind_group_layout m_adapt_layout{};

        gpu::pipeline m_luminance_pipeline{};
        gpu::pipeline m_downsample_pipeline{};
        gpu::pipeline m_adapt_pipeline{};
        gpu::pipeline m_store_pipeline{};

        // Whether this frame's record() meters (enabled and a camera),
        // decided by prepare().
        bool m_meters{false};
    };
} // namespace rendering_engine
