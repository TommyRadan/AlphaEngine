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
     * Each view that composites the UI gets a block and a group of its own
     * (see @ref view_data), at the view's size: @ref prepare rewrites the
     * block, inside the frame bracket, whenever the view's size differs
     * from the one it was written for.
     *
     * Runs for every view that composites the UI (a view on the
     * swapchain whose camera draws it), in the view's rectangle of the
     * swapchain (@ref frame_resources::output), with the view's pixel size
     * as the drawable the elements are laid out in; there is no camera
     * gate. When the scene pass was skipped, the UI is composited over the
     * swapchain's initial (black) clear.
     */
    struct ui_pass : pass
    {
        explicit ui_pass(gpu::device& device);
        ~ui_pass() override;

        ui_pass(const ui_pass&) = delete;
        ui_pass& operator=(const ui_pass&) = delete;

        // Rewrites the view's UiFrame block when the view's size changed,
        // looks up the view's output and sorts this frame's draw items.
        void prepare(const frame_context& ctx) override;

        // Draws the items @ref prepare sorted over the view's rectangle of
        // its output.
        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return builtin_passes::ui;
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read(frame_resources::output);
            io.write(frame_resources::output);
        }

        // The per-frame layout the ui material template is built
        // against (see @c ui_material::create_template).
        gpu::bind_group_layout frame_bind_group_layout() const;

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

        // What the pass keeps per view: the UiFrame block, the group over
        // it, and the view size the block was written for (0 x 0 until the
        // view's first prepare() writes it).
        struct view_data final : pass_view_state
        {
            view_data(gpu::device& device, gpu::bind_group_layout frame_layout);
            ~view_data() override;

            view_data(const view_data&) = delete;
            view_data& operator=(const view_data&) = delete;

            // Rewrites the UiFrame block for @p width x @p height.
            void write_frame_block(uint32_t width, uint32_t height);

            gpu::device* device{nullptr};
            gpu::buffer frame_ubo{};
            gpu::bind_group frame_bind_group{};
            uint32_t width{0};
            uint32_t height{0};
        };

        gpu::bind_group_layout m_frame_layout{};

        // The view's group this frame binds, looked up by prepare().
        gpu::bind_group m_frame_bind_group{};

        // Reused across frames so the underlying allocation persists.
        // Copied and sorted by prepare(), drawn by record().
        std::vector<draw_item> m_items;

        // The view's output this frame composites over, looked up by
        // prepare().
        view_output m_output{};
    };
} // namespace rendering_engine
