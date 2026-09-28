// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/frame_resources.hpp>
#include <rendering_engine/passes/pass.hpp>

namespace rendering_engine
{
    /**
     * @brief Draws a cube-map background into the HDR scene target.
     *
     * Runs after the @ref scene_pass and loads (does not clear) that pass's
     * HDR colour and depth. A single fullscreen triangle is emitted at the
     * far plane (clip @c z = w) with depth testing on but depth writes off,
     * so the sky fills only the texels the scene left at the cleared far
     * depth and scene geometry occludes it everywhere else. Each fragment
     * reconstructs its world-space view direction from the inverse of the
     * camera's projection times its translation-free view matrix and
     * samples the cube, so the sky stays fixed to the world while the
     * camera turns. The HDR result flows through the existing bloom and
     * tonemap chain like any other scene colour.
     *
     * The cube map is the skybox of the world's environment probe
     * (@ref render_world::environment, set through
     * @ref renderer::set_environment), which @ref prepare follows every
     * frame. The pass is always present in the pass list but inert while
     * the world has none (and skipped on no-camera frames), mirroring how
     * the engine keeps the bloom pass resident but dormant when disabled.
     */
    struct skybox_pass : pass
    {
        explicit skybox_pass(gpu::device& device);
        ~skybox_pass() override;

        skybox_pass(const skybox_pass&) = delete;
        skybox_pass& operator=(const skybox_pass&) = delete;

        // Follows the world's environment cube map, decides whether the
        // frame draws the sky and uploads the jittered inverse
        // view-projection it unprojects with.
        void prepare(const frame_context& ctx) override;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return builtin_passes::skybox;
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read(frame_resources::scene_color);
            io.write(frame_resources::scene_color);
            // Depth is loaded for the less-equal test and stored back
            // (writes are off, but the attachment round-trips through the
            // pass), so declare it on both sides: the graph then orders
            // any depth consumer after the sky has been composited.
            io.read(frame_resources::scene_depth);
            io.write(frame_resources::scene_depth);
        }

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

        // What the pass keeps per view: the sky UBO holding the view's
        // inverse view-projection, the input bind group over it and the
        // cube map, and the cube map that group samples.
        struct view_data final : pass_view_state
        {
            explicit view_data(gpu::device& device);
            ~view_data() override;

            view_data(const view_data&) = delete;
            view_data& operator=(const view_data&) = delete;

            gpu::device* device{nullptr};
            gpu::buffer sky_ubo{};
            gpu::bind_group input_bind_group{};
            gpu::texture bound_cubemap{};
        };

        // Rebuilds @p view's input bind group against @p cubemap and the
        // view's sky UBO. Called by @ref prepare when the cube map the
        // group samples changed.
        void rebuild_bind_group(view_data& view, gpu::texture cubemap);

        // The scene colour target and input bind group this frame's
        // record() draws with, looked up by @ref prepare.
        gpu::render_target m_target{};
        gpu::bind_group m_input_bind_group{};

        // Whether this frame's record() draws (a cube map and a camera),
        // decided by prepare().
        bool m_draws{false};

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_fragment_shader{};
        gpu::buffer m_vertex_buffer{};
        gpu::bind_group_layout m_input_layout{};
        gpu::pipeline m_pipeline{};
    };
} // namespace rendering_engine
