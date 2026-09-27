/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <cstdint>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/render_graph/frame_graph.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    struct renderable;

    /**
     * @brief 2D overlay pass. Loads the previous colour, disables
     *        depth, collects draw items from the UI-renderable
     *        registry, sorts them by pipeline, and dispatches them.
     *        Every overlay reaches the pass as a registered
     *        renderable's @ref draw_item; @ref record runs no event
     *        listener.
     *
     * Owns the UI's per-frame state: the @c UiFrame block the
     * @c ui_material reads at slot 0 — an orthographic projection that
     * maps drawable pixels, (0, 0) at the top-left and +y down, onto
     * clip space, plus the drawable's pixel size the vertex shader
     * resolves anchors against — and the bind group over it, bound for
     * every draw whose material was built on @ref frame_bind_group_layout.
     * The block follows the drawable: @ref resize notes the new size and
     * the next @ref record rewrites the buffer inside the frame bracket.
     *
     * Always runs; there is no camera gate. When the scene pass was
     * skipped, the UI is composited over the swapchain's initial
     * (black) clear.
     */
    struct ui_pass : pass
    {
        // @p width x @p height is the drawable's pixel size at
        // construction; @ref resize follows it from then on.
        ui_pass(std::vector<renderable*>* registry, uint32_t width, uint32_t height);
        ~ui_pass() override;

        ui_pass(const ui_pass&) = delete;
        ui_pass& operator=(const ui_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        void resize(uint32_t width, uint32_t height) override;

        const char* name() const override
        {
            return "ui";
        }

        void declare_io(render_graph::pass_io_builder& io) const override
        {
            io.read("swapchain");
            io.write("swapchain");
        }

        // The per-frame layout the ui material template is built
        // against (see @c ui_material::create_template).
        gpu::bind_group_layout frame_bind_group_layout() const;

    private:
        // Rewrites the UiFrame block for m_width x m_height.
        void write_frame_block();

        // Non-owning back-pointer to the engine context's
        // ui-renderable registry. Same lifetime guarantee as
        // @ref scene_pass::m_registry.
        std::vector<renderable*>* m_registry;

        gpu::bind_group_layout m_frame_layout{};
        gpu::buffer m_frame_ubo{};
        gpu::bind_group m_frame_bind_group{};

        // The drawable size the block is (or, while dirty, is about to
        // be) built for, and whether record still has to write it.
        uint32_t m_width{0};
        uint32_t m_height{0};
        bool m_frame_dirty{true};

        // Reused across frames so the underlying allocation persists.
        std::vector<draw_item> m_items;
    };
} // namespace rendering_engine
