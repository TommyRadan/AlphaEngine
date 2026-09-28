// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/frame_resources.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    /**
     * @brief 2D overlay pass. Loads the previous colour, disables
     *        depth, takes the frame's UI draws
     *        (@ref frame_context::ui_draws, built from the UI proxies in
     *        paint order), sorts them by pipeline, and dispatches them.
     *        @ref record runs no event listener.
     *
     * Owns the UI's per-frame state: the @c UiFrame block the
     * @c ui_material reads at slot 0 — an orthographic projection that
     * maps drawable pixels, (0, 0) at the top-left and +y down, onto
     * clip space, plus the drawable's pixel size the vertex shader
     * resolves anchors against — and the bind group over it, bound for
     * every draw whose material was built on @ref frame_bind_group_layout.
     * The block follows the drawable: @ref resize notes the new size and
     * the next @ref prepare rewrites the buffer inside the frame bracket.
     *
     * Always runs; there is no camera gate. When the scene pass was
     * skipped, the UI is composited over the swapchain's initial
     * (black) clear.
     */
    struct ui_pass : pass
    {
        // @p width x @p height is the drawable's pixel size at
        // construction; @ref resize follows it from then on.
        ui_pass(gpu::device& device, uint32_t width, uint32_t height);
        ~ui_pass() override;

        ui_pass(const ui_pass&) = delete;
        ui_pass& operator=(const ui_pass&) = delete;

        // Rewrites the UiFrame block after a resize, looks up the swapchain
        // target and sorts this frame's draw items.
        void prepare(const frame_context& ctx) override;

        // Draws the items @ref prepare sorted over the swapchain.
        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        void resize(uint32_t width, uint32_t height) override;

        const char* name() const override
        {
            return builtin_passes::ui;
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read(frame_resources::swapchain);
            io.write(frame_resources::swapchain);
        }

        // The per-frame layout the ui material template is built
        // against (see @c ui_material::create_template).
        gpu::bind_group_layout frame_bind_group_layout() const;

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

        // Rewrites the UiFrame block for m_width x m_height.
        void write_frame_block();

        gpu::bind_group_layout m_frame_layout{};
        gpu::buffer m_frame_ubo{};
        gpu::bind_group m_frame_bind_group{};

        // The drawable size the block is (or, while dirty, is about to
        // be) built for, and whether prepare still has to write it.
        uint32_t m_width{0};
        uint32_t m_height{0};
        bool m_frame_dirty{true};

        // Reused across frames so the underlying allocation persists.
        // Copied and sorted by prepare(), drawn by record().
        std::vector<draw_item> m_items;

        // The swapchain target this frame composites over, looked up by
        // prepare().
        gpu::render_target m_target{};
    };
} // namespace rendering_engine
