// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <array>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/frame_resources.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/post_settings.hpp>

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
     * bloomed HDR result to LDR exactly as before. While
     * @ref motion_blur_pass runs, "the scene colour" is its blurred copy:
     * the pass reads and composites into whichever image
     * @ref frame_resources::scene_color holds when it prepares. Every pass
     * uses the
     * shared fullscreen-triangle pattern (depth off, no culling, no
     * vertex buffers beyond the @ref fullscreen_triangle_vertices).
     *
     * The threshold / knee and the per-level composite weights are baked
     * into per-stage UBOs when a view's pyramid is built (on the view's
     * first frame and again when its size changes, from whichever
     * @ref bloom_settings were last applied) and are live-tunable:
     * @ref prepare compares
     * @ref frame_context::post's @c bloom fields against what it last
     * uploaded and rewrites only the UBO(s) a changed field affects. The
     * blur offsets stay static — they depend only on the target
     * dimensions, mirroring the way @ref fxaa_pass bakes its edge step.
     * @ref bloom_settings::enabled early-outs @ref record entirely,
     * leaving the HDR scene colour it would have brightened untouched,
     * rather than removing the pass from the pass list.
     */
    struct bloom_pass : pass
    {
        // The HDR image the bright-pass samples is not a constructor
        // input: it is looked up every frame (@ref frame_resources::scene_color:
        // the scene colour, or motion blur's output while that runs), and a
        // view's threshold bind group is (re)built whenever that handle
        // differs from the one it was last built against. The composite
        // target is the same image's target. The mip pyramid is sized
        // against each view's size (see @ref view_data).
        explicit bloom_pass(gpu::device& device);
        ~bloom_pass() override;

        bloom_pass(const bloom_pass&) = delete;
        bloom_pass& operator=(const bloom_pass&) = delete;

        // Decides whether the frame blooms, rewrites the UBO(s) a changed
        // setting affects and rebinds the HDR input when its handle changed.
        void prepare(const frame_context& ctx) override;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return builtin_passes::bloom;
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read(frame_resources::scene_color);
            io.write(frame_resources::scene_color);
        }

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

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

        // What the pass keeps per view: the half-resolution bright-pass
        // output (also the input to mip 0) with its threshold UBO and bind
        // group, the HDR texture that group was built against (invalid
        // until the view's first prepare() builds it), the blur pyramid
        // sized against the view (@ref width x @ref height), and the
        // bloom_settings currently baked into the threshold UBO and every
        // level's weight UBO. The settings start at the struct's own
        // compiled-in defaults, so a caller that never touches
        // frame_context::post sees those; the pyramid is baked from them
        // (so a resize rebuilds at whatever was last applied) and prepare()
        // updates them as it rewrites a UBO.
        struct view_data final : pass_view_state
        {
            explicit view_data(gpu::device& device) : device{&device} {}
            ~view_data() override;

            view_data(const view_data&) = delete;
            view_data& operator=(const view_data&) = delete;

            // Releases the bright-pass target and the pyramid, bind groups
            // first.
            void release_pyramid();

            gpu::device* device{nullptr};
            gpu::render_target bright_target{};
            gpu::texture bright_texture{};
            gpu::buffer threshold_ubo{};
            gpu::bind_group threshold_bind_group{};
            gpu::texture bound_scene_color{};
            std::vector<bloom_level> levels;
            bloom_settings settings{};
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

        // Resource helpers shared by every stage: an rgba16f target of the
        // given size, a static vec4 params UBO, and a {texture @0, ubo @1}
        // bind group on @ref m_io_layout.
        gpu::render_target create_target(uint32_t width, uint32_t height) const;
        gpu::buffer create_params_ubo(const std::array<float, 4>& values) const;
        gpu::bind_group create_bind_group(gpu::texture input, gpu::buffer ubo) const;

        // Builds @p view's bright-pass target and blur pyramid (targets,
        // per-level UBOs and bind groups) for a @p width x @p height view.
        // Expects the view's pyramid to be empty.
        void create_pyramid(view_data& view, uint32_t width, uint32_t height);

        // Rebuild @p view's threshold bind group against @p scene_color
        // (this frame's HDR image) and its threshold UBO, remembering the
        // handle.
        void rebuild_threshold_bind_group(view_data& view, gpu::texture scene_color);

        // Rewrites @p view's threshold UBO from {threshold, knee}; called
        // from prepare() whenever either differs from the view's settings.
        static void write_threshold_ubo(view_data& view, float threshold, float knee);

        // Rewrites every level's weight UBO of @p view from strength;
        // called from prepare() whenever it differs from the view's
        // settings.
        static void write_weights(view_data& view, float strength);

        // Whether this frame's record() draws (bloom is enabled), decided by
        // prepare().
        bool m_draws{false};

        // The HDR target the composite blends into this frame, looked up
        // by prepare().
        gpu::render_target m_target{};
    };
} // namespace rendering_engine
