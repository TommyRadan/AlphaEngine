// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file overlay_renderer.hpp
 * @brief @c gpu::overlay_renderer — the GPU half of a Dear ImGui overlay.
 *
 * Dear ImGui builds its frame on the CPU and hands the result to a
 * renderer backend; this is the interface every GPU backend implements
 * for that half, so the code that owns the ImGui context never names a
 * backend type. The implementation draws the current ImGui context's
 * draw data, so nothing in this header depends on ImGui. Created through
 * @ref device::create_overlay_renderer, and only in builds that link
 * ImGui.
 */

#pragma once

#include <cstdint>
#include <span>

#include <rendering_engine/gpu/handle.hpp>

namespace rendering_engine::gpu
{
    struct render_pass_encoder;

    /**
     * @brief A texture as the overlay draws it: the id ImGui's image
     *        widgets take (an @c ImTextureID; 0 when the texture cannot
     *        be shown) and its size in texels.
     */
    struct overlay_texture
    {
        uint64_t id{0};
        uint32_t width{0};
        uint32_t height{0};
    };

    /**
     * @brief Draws a Dear ImGui context's frames into the swapchain pass.
     *
     * The frame is split in two: @ref new_frame opens it before
     * @c ImGui::NewFrame, and once @c ImGui::Render has built the draw
     * data, @ref render records it into the swapchain render pass the
     * debug pass has open. The renderer rebuilds whatever it baked
     * against that pass (the Vulkan pipeline) when the pass it is handed
     * changes, as it does when the swapchain is rebuilt. Main-thread only.
     */
    struct overlay_renderer
    {
        // Out-of-line virtual destructor: pins the vtable and typeinfo
        // to @c overlay_renderer.cpp.
        virtual ~overlay_renderer();

        /**
         * @brief Brings the renderer up on its device for the current
         *        ImGui context, against the swapchain render pass the
         *        debug pass begins. Returns false, with the reason logged,
         *        when it could not; nothing needs shutting down then.
         */
        virtual bool init() = 0;

        /**
         * @brief Releases every GPU resource the renderer holds, once the
         *        device has retired every frame that could still read
         *        them. Call before the ImGui context is destroyed.
         */
        virtual void shutdown() = 0;

        /** @brief Opens an overlay frame; call before @c ImGui::NewFrame. */
        virtual void new_frame() = 0;

        /**
         * @brief Records the draw data of the frame opened by the last
         *        @ref new_frame into @p encoder, the swapchain render pass
         *        the debug pass has open, once.
         *
         * Records nothing when no frame was opened since the last call,
         * or when @p encoder's pass is not open (no swapchain image this
         * frame).
         */
        virtual void render(render_pass_encoder& encoder) = 0;

        /**
         * @brief @p handle as ImGui's image widgets draw it, registered on
         *        first use and kept until @ref retain_textures drops it.
         *        A texture that is recreated (a resize) is registered
         *        again. Returns a zero id for a handle that names no live
         *        texture.
         */
        virtual overlay_texture texture(gpu::texture handle) = 0;

        /**
         * @brief Drops the registration of every texture whose handle id
         *        is not in @p ids. A frame still in flight may draw a
         *        dropped one, so the release waits for that frame.
         */
        virtual void retain_textures(std::span<const uint64_t> ids) = 0;
    };
} // namespace rendering_engine::gpu
