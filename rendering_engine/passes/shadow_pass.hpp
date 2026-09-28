// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/passes/shadow_casters.hpp>
#include <rendering_engine/passes/shadow_settings.hpp>
#include <rendering_engine/renderables/draw_item.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine
{
    struct renderable;

    // Upper bound of the directional cascades: the lit shaders' Shadow
    // block holds this many light-space matrices and split depths
    // (SHADOW_MAX_CASCADES in shaders/include/shadows.glsl).
    constexpr int max_shadow_cascades = static_cast<int>(rendering_engine::shadow_settings::max_cascade_count);

    /**
     * @brief Directional-light cascaded shadow-map pass.
     *
     * Runs ahead of the @ref scene_pass and renders the scene's depth
     * from the first shadow-casting @ref directional_light's point of
     * view into one @c depth32_float 2D-array texture, one layer per
     * cascade, each layer filled through its own depth-only target. The
     * @ref scene_pass exposes that array, its comparison sampler, the
     * per-cascade light-space matrices and split depths to the lit
     * materials through its per-frame bind group; the lit fragment
     * shaders pick a cascade by view depth, cross-fade across a small
     * band before each split, and filter with hardware PCF.
     *
     * With a camera attached (@ref frame_context::active_camera) its view
     * depth, from the near plane out to the configured shadow distance,
     * is split into slices by a log / uniform blend. Each slice is
     * enclosed in its bounding sphere — rotation-invariant, so a cascade
     * does not pulse as the camera turns — and gets an orthographic box
     * along the light direction whose centre is snapped to whole texels
     * so its edges do not crawl as the camera moves. The box reaches
     * back toward the light far enough to hold every caster whose bounds
     * overlap it, and a caster whose bounds cannot reach a cascade is
     * culled for that cascade. With no camera a single cascade falls back
     * to a fixed box centred on the world origin. When no directional
     * light has @c cast_shadow set the pass still clears the layers and
     * reports @ref has_shadow as false so the lit shaders fall back to
     * unshadowed lighting.
     *
     * The pass reuses the same scene-renderable registry as the
     * @ref scene_pass and the per-draw model-matrix bind group each
     * renderable already builds (binding 1), so every scene renderable
     * casts without any per-renderable wiring; only the depth-only
     * pipeline (vertex stage only, rasteriser depth bias against acne)
     * and the light-space matrices differ. Instanced batches cast
     * through the instanced twin of that pipeline, which reads their
     * per-instance transform stream (see @ref shadow_caster_dispatch). A
     * renderable also needs @ref renderable::casts_shadow and a
     * @ref renderable::layer_mask that overlaps @ref caster_mask to reach
     * the map; both default to "every renderable casts".
     */
    struct shadow_pass : pass
    {
        // @p settings supplies the map resolution, the shadow distance,
        // the cascade count, the receiver and slope biases and the PCF
        // kernel; they are fixed for the pass's lifetime.
        shadow_pass(gpu::device& device,
                    const std::vector<renderable*>* registry,
                    const rendering_engine::shadow_settings& settings);
        ~shadow_pass() override;

        shadow_pass(const shadow_pass&) = delete;
        shadow_pass& operator=(const shadow_pass&) = delete;

        // Finds the caster, fits the cascades to the frame's camera,
        // culls and collects the casters, and uploads each active
        // cascade's matrix; every accessor below reports this frame from
        // here on. Runs ahead of the scene pass's prepare, which reads
        // them.
        void prepare(const frame_context& ctx) override;

        // Clears every cascade layer and draws the casters @ref prepare
        // collected into the active ones.
        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "shadow";
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.write("shadow_map");
        }

        // Depth 2D-array texture the cascades are rendered into, one
        // layer each. Stable for the pass's lifetime so the
        // @ref scene_pass can bake it into its per-frame bind group once
        // at construction.
        gpu::texture shadow_map() const;

        // Depth-comparison sampler (linear, less-equal) the lit shaders
        // read @ref shadow_map through as a @c sampler2DArrayShadow, so
        // each tap is a hardware-filtered PCF lookup. Bound at the shadow
        // map's binding number; stable for the pass's lifetime.
        gpu::sampler shadow_sampler() const;

        // Cascades holding usable depth this frame: the configured count
        // with a camera, one for the camera-less fallback box, 0 when
        // @ref has_shadow is false.
        int cascade_count() const;

        // Light-space view-projection of cascade @p cascade, refreshed
        // every @ref prepare. Only meaningful below @ref cascade_count.
        const core::math::mat4& light_view_projection(int cascade) const;

        // View depth at which cascade @p cascade ends and the next one
        // takes over (the last ends at the shadow distance).
        float split_depth(int cascade) const;

        // Receiver-side depth-comparison bias of cascade @p cascade in
        // its map's [0, 1] depth units, which the lit shader slope-scales
        // to suppress shadow acne. Scaled per cascade so every cascade is
        // biased by the same world distance per unit of its radius,
        // however far its box reaches back toward the light.
        float depth_bias(int cascade) const;

        // Width of the band before each split over which the lit shader
        // cross-fades into the next cascade, as a fraction of the
        // cascade's depth range.
        float cascade_blend() const;

        // Hardware-filtered PCF taps per side of the lit shader's kernel.
        uint32_t pcf_kernel() const;

        // Whether a shadow-casting directional light was found this
        // frame and the shadow map holds usable depth.
        bool has_shadow() const;

        // Index of the caster within the packed directional-light array
        // (matching @ref pack_lights ordering) so the lit shader only
        // shadows that one light. -1 when @ref has_shadow is false.
        int shadow_light_index() const;

        // Caster / cascade pairs skipped by the last @ref prepare because
        // the caster's world bounds could not reach that cascade (a
        // caster outside every cascade counts once per cascade). Zero on
        // no-caster frames.
        uint32_t culled_count() const;

        // Layer bits this pass accepts casters from, on top of the
        // existing @ref renderable::casts_shadow filter: a renderable
        // whose layer_mask shares no bit with this mask casts no shadow
        // through it. Defaults to @ref layer_all, so nothing changes until
        // a caller narrows it.
        void set_caster_mask(uint32_t mask) noexcept;
        uint32_t caster_mask() const noexcept;

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

        // One shadow caster's slice of @ref m_items and the cascades its
        // bounds reach (bit n for cascade n), recorded once per frame so
        // each cascade draws only its own casters without re-walking the
        // registry. A renderable that reports no bounds reaches every
        // cascade.
        struct caster_range
        {
            std::size_t first{0};
            std::size_t count{0};
            uint32_t cascades{0};
        };

        // Non-owning back-pointer to the render world's
        // scene-renderable registry — the same one the scene pass
        // walks. The world outlives every pass.
        const std::vector<renderable*>* m_registry;

        // Configuration, fixed at construction (see rendering_engine::shadow_settings).
        uint32_t m_resolution{0};
        int m_cascade_count{1};
        float m_distance{0.0f};
        float m_bias{0.0f};
        uint32_t m_pcf_kernel{1};

        // The cascade depth array, one depth-only target per layer (each
        // importing its layer), and the comparison sampler the lit
        // shaders read the array through. The array is owned here (the
        // targets import it), so it is released after them.
        gpu::texture m_depth_texture{};
        std::array<gpu::render_target, max_shadow_cascades> m_targets{};
        gpu::sampler m_compare_sampler{};

        gpu::shader_module m_vertex_shader{};
        gpu::pipeline m_pipeline{};

        // The instanced twin of @ref m_pipeline for instanced casters;
        // shares the light layout and the fixed-function state.
        instanced_shadow_pipeline m_instanced{};

        // Per-cascade light bind groups (slot 0): each cascade's
        // light-space view-projection at binding 0. The per-draw model
        // matrix is each caster's pushed PerDraw block.
        gpu::bind_group_layout m_light_layout{};
        std::array<gpu::buffer, max_shadow_cascades> m_light_ubos{};
        std::array<gpu::bind_group, max_shadow_cascades> m_light_bind_groups{};

        // Reused across frames so the allocations persist. Collected
        // once per frame by prepare(), ahead of the cascades.
        std::vector<draw_item> m_items;
        std::vector<caster_range> m_casters;

        std::array<core::math::mat4, max_shadow_cascades> m_light_view_projections{};
        std::array<float, max_shadow_cascades> m_split_depths{};
        std::array<float, max_shadow_cascades> m_depth_biases{};
        int m_active_cascades{0};
        bool m_has_shadow{false};
        int m_shadow_light_index{-1};
        uint32_t m_culled{0};
        uint32_t m_caster_mask{layer_all};
    };
} // namespace rendering_engine
