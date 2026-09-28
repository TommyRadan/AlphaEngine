// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file view.hpp
 * @brief A view: one camera rendered into one rectangle of one target, once
 *        per frame.
 */

#pragma once

#include <cstdint>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/render_proxies.hpp>

namespace rendering_engine
{
    /**
     * @brief One entry of the list of views the renderer renders in a frame.
     *
     * Built once per frame by @ref render_world::collect_views from the
     * camera proxies, in the order the renderer renders them. A view carries
     * the camera it renders (the proxy holds the view, projection, frustum
     * and culling mask), where its image lands — its target and the
     * rectangle within it — and its rank. Everything the view keeps across
     * frames lives in the renderer's per-view resource set
     * (@ref view_resources), keyed by @ref camera, the view's identity.
     */
    struct view
    {
        // The camera proxy the view renders, its identity from frame to
        // frame; invalid for the camera-less view the renderer puts on the
        // swapchain on a frame no camera renders to it.
        camera_proxy_handle camera{};

        // The proxy @ref camera names, valid for the frame the list was
        // built for; null for the camera-less view.
        const camera_proxy* proxy{nullptr};

        // Where the view's image lands: a render texture's target, or an
        // invalid target for the swapchain.
        gpu::render_target target{};

        // The view's rectangle within its target, in pixels from the
        // target's bottom-left corner, and the target's own size. Its size is
        // the size of the view's off-screen targets.
        int32_t x{0};
        int32_t y{0};
        uint32_t width{0};
        uint32_t height{0};
        uint32_t target_width{0};
        uint32_t target_height{0};

        // Whether the view composites the game UI over its image; only a
        // view on the swapchain does.
        bool ui{true};

        /** @brief Whether the view renders to the swapchain rather than a render texture. */
        bool on_swapchain() const noexcept
        {
            return !target.valid();
        }
    };
} // namespace rendering_engine
