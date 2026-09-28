// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file render_proxies.hpp
 * @brief The renderer's copies of what the world places: light and camera
 *        proxies (mesh and UI proxies are in mesh_proxy.hpp and
 *        ui_proxy.hpp), the handles that name every proxy in a
 *        @ref render_world, and the layer bits drawables and cameras filter
 *        each other by.
 */

#pragma once

#include <cstdint>

#include <core/math/frustum.hpp>
#include <core/math/mat4.hpp>
#include <core/math/vec3.hpp>
#include <core/pool.hpp>

namespace rendering_engine
{
    // Bit 0: the layer every mesh proxy is on by default
    // (@ref mesh_description::layer_mask). A pass or camera that does not
    // care about layers filters with @ref layer_all, which includes this
    // bit, so nothing is excluded until a caller narrows either side.
    constexpr uint32_t layer_default = 1u << 0;

    // Editor-only geometry: the debug geometry (the editor's ground grid and
    // the debug-draw line batches) carries this bit instead of
    // @ref layer_default. It is included in @ref layer_all, so a fresh
    // camera still renders it and nothing changes visually by default; a
    // game builds a camera whose @ref camera::set_culling_mask clears this
    // bit to hide editor gizmos from gameplay views while an editor viewport
    // (the default mask) keeps seeing them.
    constexpr uint32_t layer_editor = 1u << 31;

    // Every layer bit set: the default @ref camera::culling_mask and the
    // default shadow-pass caster mask, so nothing is excluded by layer
    // until a caller narrows one of them.
    constexpr uint32_t layer_all = ~0u;

    // Discriminator for the concrete light kind. The scene pass reads
    // it to route each light into the matching slot of the packed lights
    // UBO without a dynamic_cast.
    enum class light_type
    {
        ambient,
        directional,
        point,
        spot,
    };

    struct light_proxy_tag;
    struct camera_proxy_tag;
    struct mesh_proxy_tag;
    struct ui_proxy_tag;

    /** @brief Names a @ref light_proxy in the @ref render_world that created it. */
    using light_proxy_handle = core::pool_handle<light_proxy_tag>;

    /** @brief Names a @ref camera_proxy in the @ref render_world that created it. */
    using camera_proxy_handle = core::pool_handle<camera_proxy_tag>;

    /** @brief Names a @ref mesh_proxy in the @ref render_world that created it. */
    using mesh_proxy_handle = core::pool_handle<mesh_proxy_tag>;

    /** @brief Names a @ref ui_proxy in the @ref render_world that created it. */
    using ui_proxy_handle = core::pool_handle<ui_proxy_tag>;

    /**
     * @brief Everything the renderer reads about one light: its kind, its
     *        emission, its world-space pose and its shadow flag.
     *
     * A plain copy owned by the @ref render_world. Whoever created it (a
     * @c runtime::light_component, through the world's extraction step)
     * writes it before the frame renders; the passes only read it, so a light
     * cannot change under a pass mid-frame.
     */
    struct light_proxy
    {
        light_type type{light_type::ambient};

        // Linear RGB radiance, multiplied by @ref intensity before upload.
        core::math::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};

        // World-space point the light radiates from (point and spot).
        core::math::vec3 position{0.0f, 0.0f, 0.0f};

        // World-space direction the light travels along, from the source
        // toward the scene (directional and spot). Need not be normalized;
        // the passes normalize it.
        core::math::vec3 direction{0.0f, 0.0f, -1.0f};

        // Distance past which a point or spot light contributes nothing; 0
        // means no hard cutoff.
        float range{0.0f};

        // Point and spot attenuation: 1 / (constant + linear*d + quadratic*d*d).
        float constant_attenuation{1.0f};
        float linear_attenuation{0.0f};
        float quadratic_attenuation{1.0f};

        // Spot cone half-angles, in radians: full intensity inside
        // @ref inner_angle, nothing past @ref outer_angle.
        float inner_angle{0.349065850f};
        float outer_angle{0.523598776f};

        // Whether the light renders a shadow map (directional, point and
        // spot); only the first caster of each kind is honoured.
        bool cast_shadow{false};
    };

    /**
     * @brief Everything the renderer reads about one camera: where it looks
     *        from, how it projects, which layers it renders and how it
     *        ranks against the other cameras.
     *
     * A plain copy owned by the @ref render_world, written by whoever created
     * it (a @c runtime::camera_component, through the world's extraction
     * step) before the frame renders. @ref view, @ref projection and
     * @ref frustum are kept consistent with @ref world by that writer, so a
     * pass reads them without recomputing anything.
     */
    struct camera_proxy
    {
        // The camera's world matrix: its translation is the eye, its +X
        // column the forward and its +Z column the up (see
        // @ref view_matrix_from_world).
        core::math::mat4 world{};

        // World-to-view matrix derived from @ref world, and the unjittered
        // projection.
        core::math::mat4 view{};
        core::math::mat4 projection{};

        // The unjittered view frustum (@c projection * @c view), for culling.
        core::math::frustum frustum{};

        // Layer bits the camera renders; see @ref layer_all.
        uint32_t culling_mask{layer_all};

        // Arbitration: among the enabled cameras the highest priority
        // renders, a tie goes to the one tagged main, then to the one created
        // last (see @ref render_world::active_camera).
        int priority{0};
        bool main{false};
        bool enabled{true};
    };
} // namespace rendering_engine
