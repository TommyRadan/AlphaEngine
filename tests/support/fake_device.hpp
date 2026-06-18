// A minimal in-memory gpu::device for tests. It implements the abstract
// device interface with stubs, handing back unique non-zero handles for the
// resources the asset layer creates (buffers, textures) and recording
// create/destroy traffic so tests can assert that asset handles release their
// GPU resources. Everything else returns a default/invalid handle — the asset
// cache never exercises pipelines, bind groups, or command encoders.

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_set>

#include <rendering_engine/gpu/device.hpp>

namespace test_support
{
    struct fake_device final : rendering_engine::gpu::device
    {
        // -- Recorded traffic, for assertions -------------------------------
        std::uint64_t created_buffers = 0;
        std::uint64_t destroyed_buffers = 0;
        std::uint64_t created_textures = 0;
        std::uint64_t destroyed_textures = 0;
        std::unordered_set<std::uint64_t> live_buffers;
        std::unordered_set<std::uint64_t> live_textures;

        std::size_t live_buffer_count() const
        {
            return live_buffers.size();
        }
        std::size_t live_texture_count() const
        {
            return live_textures.size();
        }

        // -- Lifecycle ------------------------------------------------------
        void init() override {}
        void quit() override {}

        // -- Resource creation ---------------------------------------------
        rendering_engine::gpu::buffer create_buffer(const rendering_engine::gpu::buffer_descriptor&) override
        {
            const std::uint64_t id = ++m_next_id;
            live_buffers.insert(id);
            ++created_buffers;
            return rendering_engine::gpu::buffer{id};
        }

        rendering_engine::gpu::texture create_texture(const rendering_engine::gpu::texture_descriptor&) override
        {
            const std::uint64_t id = ++m_next_id;
            live_textures.insert(id);
            ++created_textures;
            return rendering_engine::gpu::texture{id};
        }

        rendering_engine::gpu::sampler create_sampler(const rendering_engine::gpu::sampler_descriptor&) override
        {
            return rendering_engine::gpu::sampler{++m_next_id};
        }

        rendering_engine::gpu::shader_module
        create_shader_module(const rendering_engine::gpu::shader_module_descriptor&) override
        {
            return rendering_engine::gpu::shader_module{++m_next_id};
        }

        rendering_engine::gpu::bind_group_layout
        create_bind_group_layout(const rendering_engine::gpu::bind_group_layout_descriptor&) override
        {
            return rendering_engine::gpu::bind_group_layout{++m_next_id};
        }

        rendering_engine::gpu::pipeline create_pipeline(const rendering_engine::gpu::pipeline_descriptor&) override
        {
            return rendering_engine::gpu::pipeline{++m_next_id};
        }

        rendering_engine::gpu::pipeline
        create_compute_pipeline(const rendering_engine::gpu::compute_pipeline_descriptor&) override
        {
            return rendering_engine::gpu::pipeline{++m_next_id};
        }

        rendering_engine::gpu::bind_group create_bind_group(const rendering_engine::gpu::bind_group_descriptor&) override
        {
            return rendering_engine::gpu::bind_group{++m_next_id};
        }

        // -- Resource destruction ------------------------------------------
        void destroy(rendering_engine::gpu::buffer handle) override
        {
            if (live_buffers.erase(handle.id) != 0)
            {
                ++destroyed_buffers;
            }
        }
        void destroy(rendering_engine::gpu::texture handle) override
        {
            if (live_textures.erase(handle.id) != 0)
            {
                ++destroyed_textures;
            }
        }
        void destroy(rendering_engine::gpu::sampler) override {}
        void destroy(rendering_engine::gpu::shader_module) override {}
        void destroy(rendering_engine::gpu::bind_group_layout) override {}
        void destroy(rendering_engine::gpu::pipeline) override {}
        void destroy(rendering_engine::gpu::bind_group) override {}

        // -- Resource updates ----------------------------------------------
        void write_buffer(rendering_engine::gpu::buffer, const void*, std::size_t, std::size_t) override {}
        void write_texture(rendering_engine::gpu::texture, const void*, std::size_t) override {}
        void write_texture_3d(rendering_engine::gpu::texture, const void*, std::size_t) override {}
        void write_cube_face(rendering_engine::gpu::texture, rendering_engine::gpu::cube_face, const void*,
                             std::size_t) override
        {
        }
        void generate_mipmaps(rendering_engine::gpu::texture) override {}

        // -- Render targets -------------------------------------------------
        rendering_engine::gpu::render_target swapchain_target() override
        {
            return rendering_engine::gpu::render_target{};
        }
        void resize_swapchain(std::uint32_t, std::uint32_t) override {}
        rendering_engine::gpu::render_target
        create_render_target(const rendering_engine::gpu::render_target_descriptor&) override
        {
            return rendering_engine::gpu::render_target{++m_next_id};
        }
        void destroy(rendering_engine::gpu::render_target) override {}
        rendering_engine::gpu::texture render_target_color_texture(rendering_engine::gpu::render_target) override
        {
            return rendering_engine::gpu::texture{};
        }
        rendering_engine::gpu::texture render_target_depth_texture(rendering_engine::gpu::render_target) override
        {
            return rendering_engine::gpu::texture{};
        }

        // -- Command recording ---------------------------------------------
        std::unique_ptr<rendering_engine::gpu::command_encoder> create_command_encoder(std::uint32_t = 0) override
        {
            return nullptr;
        }
        void submit(std::unique_ptr<rendering_engine::gpu::command_encoder>) override {}

    private:
        std::uint64_t m_next_id = 0;
    };
} // namespace test_support
