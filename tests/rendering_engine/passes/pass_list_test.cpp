// Unit tests for pass_list, the renderer's ordered pass list:
// pass_io_builder bookkeeping, validate()'s produced-before-read check
// (imported resources, in-order writes, same-pass read+write, reads ahead of
// their producer), recording in the order the passes were added, clear(),
// and a mirror of the engine's built-in pass declarations — with temporal AA
// on and off — that must validate hazard-free so a mis-declared read (the
// velocity pass once declared "scene_color" while sampling the depth
// attachment; FXAA once declared "ldr_color" while sampling the TAA resolve)
// is caught headless.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/passes/pass_list.hpp>

namespace
{
    namespace gpu = rendering_engine::gpu;
    using rendering_engine::frame_context;
    using rendering_engine::pass_io_builder;
    using rendering_engine::pass_list;

    // The list only forwards the encoder to each pass, so a stub that
    // records nothing is enough to drive record().
    struct null_encoder final : gpu::command_encoder
    {
        std::unique_ptr<gpu::render_pass_encoder> begin_render_pass(const gpu::render_pass_descriptor&) override
        {
            return nullptr;
        }

        std::unique_ptr<gpu::compute_pass_encoder> begin_compute_pass() override
        {
            return nullptr;
        }

        void copy_buffer_to_buffer(gpu::buffer, size_t, gpu::buffer, size_t, size_t) override
        {
        }

        void clear_buffer(gpu::buffer, size_t, size_t, uint32_t) override
        {
        }

        void barrier(gpu::pipeline_stage, gpu::pipeline_stage, gpu::access_flag, gpu::access_flag) override
        {
        }

        void copy_buffer_to_texture(gpu::buffer, size_t, gpu::texture, const gpu::texture_copy_region&) override
        {
        }

        void copy_texture_to_buffer(gpu::texture, const gpu::texture_copy_region&, gpu::buffer, size_t) override
        {
        }

        void push_debug_group(const char*) override
        {
        }

        void pop_debug_group() override
        {
        }

        void reset_queries(gpu::query_set, uint32_t, uint32_t) override
        {
        }

        void write_timestamp(gpu::query_set, uint32_t) override
        {
        }
    };

    using record_fn = std::function<void(gpu::command_encoder&, const frame_context&)>;

    // A pass that declares @p io and calls @p on_record (when set) as it is
    // recorded.
    struct stub_pass final : rendering_engine::pass
    {
        stub_pass(std::string name, pass_io_builder io, record_fn on_record)
            : m_name{std::move(name)}, m_io{std::move(io)}, m_on_record{std::move(on_record)}
        {
        }

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override
        {
            if (m_on_record)
            {
                m_on_record(encoder, ctx);
            }
        }

        const char* name() const override
        {
            return m_name.c_str();
        }

        void declare_io(pass_io_builder& io) const override
        {
            for (const std::string& r : m_io.reads())
            {
                io.read(r);
            }
            for (const std::string& w : m_io.writes())
            {
                io.write(w);
            }
        }

    private:
        std::string m_name;
        pass_io_builder m_io;
        record_fn m_on_record;
    };

    void add_pass(pass_list& passes, std::string name, pass_io_builder io, record_fn on_record)
    {
        passes.add(std::make_unique<stub_pass>(std::move(name), std::move(io), std::move(on_record)));
    }

    pass_io_builder io_of(std::initializer_list<const char*> reads, std::initializer_list<const char*> writes)
    {
        pass_io_builder io;
        for (const char* r : reads)
        {
            io.read(r);
        }
        for (const char* w : writes)
        {
            io.write(w);
        }
        return io;
    }

    // Callback that appends @p name to @p log when the pass runs.
    record_fn trace(std::vector<std::string>& log, std::string name)
    {
        return [&log, name = std::move(name)](gpu::command_encoder&, const frame_context&) { log.push_back(name); };
    }

    // The engine's built-in pass list as rendering_engine::renderer::init
    // registers it (debug build), with each pass's declare_io transcribed.
    // With temporal AA on the velocity and TAA passes are present, the TAA
    // history is imported, and FXAA reads the TAA resolve; with it off
    // neither pass exists and FXAA reads the tonemapped LDR target. Kept in
    // step by hand: a pass whose declaration changes should update this
    // mirror too.
    void add_engine_passes(pass_list& passes, bool taa_enabled)
    {
        passes.import_external("swapchain");
        if (taa_enabled)
        {
            passes.import_external("taa_history");
        }
        add_pass(passes, "shadow", io_of({}, {"shadow_map"}), {});
        add_pass(passes, "point_shadow", io_of({}, {"point_shadow"}), {});
        add_pass(passes, "scene", io_of({"shadow_map", "point_shadow"}, {"scene_color", "scene_depth"}), {});
        add_pass(passes, "skybox", io_of({"scene_color", "scene_depth"}, {"scene_color", "scene_depth"}), {});
        if (taa_enabled)
        {
            add_pass(passes, "velocity", io_of({"scene_depth"}, {"velocity"}), {});
        }
        add_pass(passes, "bloom", io_of({"scene_color"}, {"scene_color"}), {});
        add_pass(passes, "tonemap", io_of({"scene_color"}, {"ldr_color"}), {});
        if (taa_enabled)
        {
            add_pass(passes,
                     "taa",
                     io_of({"ldr_color", "velocity", "taa_history"}, {"taa_resolve", "taa_history"}),
                     {});
        }
        add_pass(passes, "fxaa", io_of({taa_enabled ? "taa_resolve" : "ldr_color"}, {"swapchain"}), {});
        add_pass(passes, "ui", io_of({"swapchain"}, {"swapchain"}), {});
        add_pass(passes, "debug", io_of({"swapchain"}, {"swapchain"}), {});
    }
} // namespace

// --- pass_io_builder --------------------------------------------------------

TEST(pass_io_builder, records_reads_and_writes_in_declaration_order)
{
    pass_io_builder io;
    io.read("b");
    io.read("a");
    io.write("c");
    io.write("a");
    EXPECT_EQ(io.reads(), (std::vector<std::string>{"b", "a"}));
    EXPECT_EQ(io.writes(), (std::vector<std::string>{"c", "a"}));
}

TEST(pass_io_builder, starts_empty)
{
    pass_io_builder io;
    EXPECT_TRUE(io.reads().empty());
    EXPECT_TRUE(io.writes().empty());
}

// --- validate ---------------------------------------------------------------

TEST(pass_list, empty_list_validates_hazard_free)
{
    pass_list passes;
    EXPECT_TRUE(passes.validate());
    EXPECT_EQ(passes.size(), 0u);
}

TEST(pass_list, pass_declaring_nothing_is_hazard_free)
{
    pass_list passes;
    add_pass(passes, "silent", pass_io_builder{}, {});
    EXPECT_TRUE(passes.validate());
}

TEST(pass_list, read_of_imported_resource_is_produced)
{
    pass_list passes;
    passes.import_external("swapchain");
    add_pass(passes, "ui", io_of({"swapchain"}, {"swapchain"}), {});
    EXPECT_TRUE(passes.validate());
}

TEST(pass_list, read_of_resource_written_by_earlier_pass_is_produced)
{
    pass_list passes;
    add_pass(passes, "scene", io_of({}, {"scene_color"}), {});
    add_pass(passes, "tonemap", io_of({"scene_color"}, {"ldr_color"}), {});
    EXPECT_TRUE(passes.validate());
}

TEST(pass_list, read_before_any_producer_is_a_hazard)
{
    pass_list passes;
    add_pass(passes, "tonemap", io_of({"scene_color"}, {"ldr_color"}), {});
    EXPECT_FALSE(passes.validate());
}

TEST(pass_list, read_ahead_of_its_producer_is_a_hazard)
{
    pass_list passes;
    add_pass(passes, "tonemap", io_of({"scene_color"}, {"ldr_color"}), {});
    add_pass(passes, "scene", io_of({}, {"scene_color"}), {});
    EXPECT_FALSE(passes.validate());
}

TEST(pass_list, same_pass_read_and_write_needs_an_earlier_producer)
{
    // A pass's own write does not satisfy its own read: bloom reads and
    // writes scene_color, so something before it must have produced it.
    pass_list unproduced;
    add_pass(unproduced, "bloom", io_of({"scene_color"}, {"scene_color"}), {});
    EXPECT_FALSE(unproduced.validate());

    pass_list produced;
    add_pass(produced, "scene", io_of({}, {"scene_color"}), {});
    add_pass(produced, "bloom", io_of({"scene_color"}, {"scene_color"}), {});
    EXPECT_TRUE(produced.validate());
}

TEST(pass_list, one_hazard_fails_the_whole_validation)
{
    pass_list passes;
    passes.import_external("swapchain");
    add_pass(passes, "scene", io_of({}, {"scene_color"}), {});
    add_pass(passes, "tonemap", io_of({"scene_color"}, {"ldr_color"}), {});
    add_pass(passes, "fxaa", io_of({"taa_resolve"}, {"swapchain"}), {});
    add_pass(passes, "ui", io_of({"swapchain"}, {"swapchain"}), {});
    EXPECT_FALSE(passes.validate());
}

TEST(pass_list, validate_is_repeatable_and_does_not_record)
{
    std::vector<std::string> log;
    pass_list passes;
    add_pass(passes, "scene", io_of({}, {"scene_color"}), trace(log, "scene"));
    add_pass(passes, "tonemap", io_of({"scene_color"}, {"ldr_color"}), trace(log, "tonemap"));
    EXPECT_TRUE(passes.validate());
    EXPECT_TRUE(passes.validate());
    EXPECT_TRUE(log.empty());
}

// --- record -----------------------------------------------------------------

TEST(pass_list, record_runs_passes_in_the_order_they_were_added)
{
    std::vector<std::string> log;
    pass_list passes;
    add_pass(passes, "shadow", pass_io_builder{}, trace(log, "shadow"));
    add_pass(passes, "scene", pass_io_builder{}, trace(log, "scene"));
    add_pass(passes, "ui", pass_io_builder{}, trace(log, "ui"));

    null_encoder encoder;
    frame_context ctx{};
    passes.record(encoder, ctx);
    EXPECT_EQ(log, (std::vector<std::string>{"shadow", "scene", "ui"}));
}

TEST(pass_list, record_forwards_the_same_encoder_and_context_to_every_pass)
{
    pass_list passes;
    std::vector<const gpu::command_encoder*> encoders;
    std::vector<const frame_context*> contexts;
    std::vector<uint64_t> depth_ids;
    for (int i = 0; i < 3; ++i)
    {
        add_pass(passes,
                 "pass",
                 pass_io_builder{},
                 [&](gpu::command_encoder& e, const frame_context& c)
                 {
                     encoders.push_back(&e);
                     contexts.push_back(&c);
                     depth_ids.push_back(c.scene_depth_texture.id);
                 });
    }

    null_encoder encoder;
    frame_context ctx{};
    ctx.scene_depth_texture.id = 42;
    passes.record(encoder, ctx);

    ASSERT_EQ(encoders.size(), 3u);
    for (size_t i = 0; i < 3; ++i)
    {
        EXPECT_EQ(encoders[i], &encoder);
        EXPECT_EQ(contexts[i], &ctx);
        EXPECT_EQ(depth_ids[i], 42u);
    }
}

TEST(pass_list, a_pass_that_records_nothing_does_not_stop_the_walk)
{
    std::vector<std::string> log;
    pass_list passes;
    add_pass(passes, "first", pass_io_builder{}, trace(log, "first"));
    add_pass(passes, "silent", pass_io_builder{}, {});
    add_pass(passes, "last", pass_io_builder{}, trace(log, "last"));

    null_encoder encoder;
    frame_context ctx{};
    EXPECT_NO_THROW(passes.record(encoder, ctx));
    EXPECT_EQ(log, (std::vector<std::string>{"first", "last"}));
}

// --- clear ------------------------------------------------------------------

TEST(pass_list, clear_drops_passes_and_imports)
{
    std::vector<std::string> log;
    pass_list passes;
    passes.import_external("swapchain");
    add_pass(passes, "ui", io_of({"swapchain"}, {"swapchain"}), trace(log, "ui"));
    ASSERT_EQ(passes.size(), 1u);
    ASSERT_TRUE(passes.validate());

    passes.clear();
    EXPECT_EQ(passes.size(), 0u);

    null_encoder encoder;
    frame_context ctx{};
    passes.record(encoder, ctx);
    EXPECT_TRUE(log.empty());

    // The import went too: the same read is now unproduced.
    add_pass(passes, "ui", io_of({"swapchain"}, {"swapchain"}), {});
    EXPECT_FALSE(passes.validate());
}

// --- the engine's pass list -------------------------------------------------

TEST(pass_list, engine_pass_declarations_validate_hazard_free_with_taa)
{
    pass_list passes;
    add_engine_passes(passes, /*taa_enabled=*/true);
    EXPECT_EQ(passes.size(), 11u);
    EXPECT_TRUE(passes.validate());
}

TEST(pass_list, engine_pass_declarations_validate_hazard_free_without_taa)
{
    pass_list passes;
    add_engine_passes(passes, /*taa_enabled=*/false);
    EXPECT_EQ(passes.size(), 9u);
    EXPECT_TRUE(passes.validate());
}

TEST(pass_list, fxaa_reading_the_taa_resolve_without_a_taa_pass_is_a_hazard)
{
    // The TAA-off list with FXAA still declaring the TAA input: the
    // resolve is never produced, so the mis-declared read flags.
    pass_list passes;
    passes.import_external("swapchain");
    add_pass(passes, "scene", io_of({}, {"scene_color", "scene_depth"}), {});
    add_pass(passes, "tonemap", io_of({"scene_color"}, {"ldr_color"}), {});
    add_pass(passes, "fxaa", io_of({"taa_resolve"}, {"swapchain"}), {});
    EXPECT_FALSE(passes.validate());
}

TEST(pass_list, scene_depth_must_be_produced_before_the_skybox_and_velocity_read_it)
{
    // Same list, but the scene pass forgets to declare its depth write: the
    // skybox's depth-tested load and the velocity pass's sample both flag.
    pass_list passes;
    passes.import_external("swapchain");
    passes.import_external("taa_history");
    add_pass(passes, "shadow", io_of({}, {"shadow_map"}), {});
    add_pass(passes, "point_shadow", io_of({}, {"point_shadow"}), {});
    add_pass(passes, "scene", io_of({"shadow_map", "point_shadow"}, {"scene_color"}), {});
    add_pass(passes, "skybox", io_of({"scene_color", "scene_depth"}, {"scene_color", "scene_depth"}), {});
    add_pass(passes, "velocity", io_of({"scene_depth"}, {"velocity"}), {});
    EXPECT_FALSE(passes.validate());
}

TEST(pass_list, velocity_ahead_of_the_scene_pass_is_a_hazard)
{
    pass_list passes;
    passes.import_external("swapchain");
    passes.import_external("taa_history");
    add_pass(passes, "velocity", io_of({"scene_depth"}, {"velocity"}), {});
    add_pass(passes, "scene", io_of({}, {"scene_color", "scene_depth"}), {});
    EXPECT_FALSE(passes.validate());
}
