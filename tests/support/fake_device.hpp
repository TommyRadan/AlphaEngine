// A minimal in-memory gpu::device for tests. It implements the abstract
// device interface with stubs, handing back unique non-zero handles for the
// resources the asset layer and the material templates create (buffers,
// textures, shader modules, layouts, pipelines, bind groups) and recording
// create/destroy traffic so tests can assert that asset handles release their
// GPU resources and that materials share pipelines. Everything else returns a
// default/invalid handle — nothing here exercises command encoders.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/texture.hpp>

namespace test_support
{
    struct fake_device final : rendering_engine::gpu::device
    {
        // -- Recorded traffic, for assertions -------------------------------
        std::uint64_t created_buffers = 0;
        std::uint64_t destroyed_buffers = 0;
        std::uint64_t created_textures = 0;
        std::uint64_t destroyed_textures = 0;
        std::uint64_t created_shader_modules = 0;
        std::uint64_t destroyed_shader_modules = 0;
        std::uint64_t created_bind_group_layouts = 0;
        std::uint64_t destroyed_bind_group_layouts = 0;
        std::uint64_t created_pipelines = 0;
        std::uint64_t destroyed_pipelines = 0;
        std::uint64_t created_bind_groups = 0;
        std::uint64_t destroyed_bind_groups = 0;
        std::unordered_set<std::uint64_t> live_buffers;
        std::unordered_set<std::uint64_t> live_textures;
        std::unordered_set<std::uint64_t> live_pipelines;
        // The descriptor of the most recent create_texture call, so a test
        // can check what format / footprint a loader asked the device for.
        rendering_engine::gpu::texture_descriptor last_texture_descriptor{};
        // The initial data of every buffer created with some, keyed by
        // handle id, so a test can read back the records a loader uploaded.
        std::unordered_map<std::uint64_t, std::vector<std::byte>> buffer_contents;
        // Every graphics pipeline descriptor handed to create_pipeline, in
        // order, so a test can check the fixed-function state a material
        // variant baked (front face, blend, depth, vertex layout).
        std::vector<rendering_engine::gpu::pipeline_descriptor> pipeline_descriptors;

        std::size_t live_buffer_count() const
        {
            return live_buffers.size();
        }
        std::size_t live_texture_count() const
        {
            return live_textures.size();
        }
        std::size_t live_pipeline_count() const
        {
            return live_pipelines.size();
        }

        // -- Lifecycle ------------------------------------------------------
        void init() override {}
        void quit() override {}

        // -- Capabilities ---------------------------------------------------
        rendering_engine::gpu::texture_usage format_support(rendering_engine::gpu::texture_format) const override
        {
            return rendering_engine::gpu::texture_usage_default | rendering_engine::gpu::texture_usage_render_attachment |
                   rendering_engine::gpu::texture_usage_storage;
        }

        // -- Resource creation ---------------------------------------------
        rendering_engine::gpu::buffer create_buffer(const rendering_engine::gpu::buffer_descriptor& descriptor) override
        {
            const std::uint64_t id = ++m_next_id;
            live_buffers.insert(id);
            ++created_buffers;
            if (descriptor.initial_data != nullptr && descriptor.size > 0)
            {
                const auto* bytes = static_cast<const std::byte*>(descriptor.initial_data);
                buffer_contents[id].assign(bytes, bytes + descriptor.size);
            }
            return rendering_engine::gpu::buffer{id};
        }

        rendering_engine::gpu::texture
        create_texture(const rendering_engine::gpu::texture_descriptor& descriptor) override
        {
            last_texture_descriptor = descriptor;
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
            ++created_shader_modules;
            return rendering_engine::gpu::shader_module{++m_next_id};
        }

        rendering_engine::gpu::bind_group_layout
        create_bind_group_layout(const rendering_engine::gpu::bind_group_layout_descriptor&) override
        {
            ++created_bind_group_layouts;
            return rendering_engine::gpu::bind_group_layout{++m_next_id};
        }

        rendering_engine::gpu::pipeline
        create_pipeline(const rendering_engine::gpu::pipeline_descriptor& descriptor) override
        {
            pipeline_descriptors.push_back(descriptor);
            const std::uint64_t id = ++m_next_id;
            live_pipelines.insert(id);
            ++created_pipelines;
            return rendering_engine::gpu::pipeline{id};
        }

        rendering_engine::gpu::pipeline
        create_compute_pipeline(const rendering_engine::gpu::compute_pipeline_descriptor&) override
        {
            return rendering_engine::gpu::pipeline{++m_next_id};
        }

        rendering_engine::gpu::bind_group create_bind_group(const rendering_engine::gpu::bind_group_descriptor&) override
        {
            ++created_bind_groups;
            return rendering_engine::gpu::bind_group{++m_next_id};
        }

        rendering_engine::gpu::query_set create_query_set(const rendering_engine::gpu::query_set_descriptor&) override
        {
            return rendering_engine::gpu::query_set{++m_next_id};
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
        void destroy(rendering_engine::gpu::shader_module) override
        {
            ++destroyed_shader_modules;
        }
        void destroy(rendering_engine::gpu::bind_group_layout) override
        {
            ++destroyed_bind_group_layouts;
        }
        void destroy(rendering_engine::gpu::pipeline handle) override
        {
            if (live_pipelines.erase(handle.id) != 0)
            {
                ++destroyed_pipelines;
            }
        }
        void destroy(rendering_engine::gpu::bind_group) override
        {
            ++destroyed_bind_groups;
        }
        void destroy(rendering_engine::gpu::query_set) override {}

        // -- Resource updates ----------------------------------------------
        void write_buffer(rendering_engine::gpu::buffer, const void*, std::size_t, std::size_t) override {}
        void write_texture(rendering_engine::gpu::texture, const void*, std::size_t) override {}
        bool write_texture_region(rendering_engine::gpu::texture,
                                  const rendering_engine::gpu::texture_write_region&,
                                  const void*,
                                  std::size_t) override
        {
            return true;
        }
        bool read_texture(rendering_engine::gpu::texture,
                          const rendering_engine::gpu::texture_copy_region&,
                          void*,
                          std::size_t) override
        {
            return false;
        }
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
        rendering_engine::gpu::texture render_target_color_texture(rendering_engine::gpu::render_target,
                                                                   std::uint32_t = 0) override
        {
            return rendering_engine::gpu::texture{};
        }
        rendering_engine::gpu::texture render_target_depth_texture(rendering_engine::gpu::render_target) override
        {
            return rendering_engine::gpu::texture{};
        }
        bool resolve_queries(rendering_engine::gpu::query_set, std::uint32_t, std::uint32_t, std::uint64_t*) override
        {
            return false;
        }

        // -- Command recording ---------------------------------------------
        std::unique_ptr<rendering_engine::gpu::command_encoder> create_command_encoder() override
        {
            return nullptr;
        }
        void submit(std::unique_ptr<rendering_engine::gpu::command_encoder>) override {}

        // -- Frame boundary -------------------------------------------------
        void begin_frame() override {}
        void end_frame() override {}

    private:
        std::uint64_t m_next_id = 0;
    };
} // namespace test_support
