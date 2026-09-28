// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/view_globals.hpp>

#include <cstdint>

#include <core/math/math.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/passes/projection_jitter.hpp>
#include <rendering_engine/post_settings.hpp>

namespace
{
    float reciprocal(uint32_t value)
    {
        return value != 0 ? 1.0f / static_cast<float>(value) : 0.0f;
    }
} // namespace

namespace rendering_engine
{
    view_globals make_view_globals(const frame_context& ctx, bool apply_jitter)
    {
        view_globals globals{};

        // The camera's own matrices are unjittered. The temporal-AA jitter
        // is a clip-space translation post-multiplied onto the projection
        // (jitter_projection), so it flows into the view-projection and the
        // inverses built from it; culling and the previous-frame matrix
        // stay unjittered.
        const core::math::mat4& view = ctx.active_camera->view;
        const core::math::mat4& projection = ctx.active_camera->projection;
        const core::math::vec2 jitter = apply_jitter ? ctx.jitter : core::math::vec2{0.0f, 0.0f};
        const core::math::vec2 prev_jitter = apply_jitter ? ctx.prev_jitter : core::math::vec2{0.0f, 0.0f};

        globals.view = view;
        globals.projection = apply_jitter ? jitter_projection(projection, ctx.jitter) : projection;
        globals.view_projection = globals.projection * view;
        globals.inverse_view = core::math::inverse(view);
        globals.inverse_projection = core::math::inverse(globals.projection);
        globals.inverse_view_projection = core::math::inverse(globals.view_projection);
        globals.prev_view_projection = ctx.has_prev_view_projection ? ctx.prev_view_projection : projection * view;

        // The camera sits at the view's inverse translation: carried here
        // once instead of every vertex inverting the view matrix.
        const float* eye = globals.inverse_view.data() + 12;
        globals.camera_position = core::math::vec4{eye[0], eye[1], eye[2], 1.0f};

        globals.viewport = core::math::vec4{static_cast<float>(ctx.viewport_width),
                                            static_cast<float>(ctx.viewport_height),
                                            reciprocal(ctx.viewport_width),
                                            reciprocal(ctx.viewport_height)};
        globals.time = core::math::vec4{ctx.time_seconds, ctx.delta_seconds, 0.0f, 0.0f};
        globals.jitter = core::math::vec4{jitter.x, jitter.y, prev_jitter.x, prev_jitter.y};

        // Fog: the mode rides in fogColor.a (the lit shaders skip the
        // distance term at 0); fogParams.w is the height-fog density (0
        // disables that term) and heightFogParams.xy its falloff and
        // reference height. heightFogParams.z is where along the view ray
        // the analytic height fog starts: while the volumetric fog pass
        // marches the same medium out to its max distance, the lit shaders
        // leave that stretch to it rather than attenuating it twice.
        const float height_fog_start =
            volumetric_fog_active(ctx.post.volumetric) ? ctx.post.volumetric.max_distance : 0.0f;
        globals.fog_color = core::math::vec4{ctx.fog.color, static_cast<float>(static_cast<int>(ctx.fog.mode))};
        globals.fog_params =
            core::math::vec4{ctx.fog.near_distance, ctx.fog.far_distance, ctx.fog.density, ctx.fog.height_density};
        globals.height_fog_params =
            core::math::vec4{ctx.fog.height_falloff, ctx.fog.reference_height, height_fog_start, 0.0f};

        return globals;
    }
} // namespace rendering_engine
