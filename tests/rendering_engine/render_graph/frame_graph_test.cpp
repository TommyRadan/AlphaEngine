// Unit tests for render_graph::frame_graph: pass_io_builder bookkeeping,
// compile()'s produced-before-read validation (imported resources, in-order
// writes, same-pass read+write, reads ahead of their producer), execution in
// registration order with execute_range clamping, clear(), and a mirror of the
// engine's built-in pass declarations that must compile hazard-free so a
// mis-declared read (the velocity pass once declared "scene_color" while
// sampling the depth attachment) is caught headless.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/render_graph/frame_graph.hpp>

namespace
{
    namespace gpu = rendering_engine::gpu;
    using rendering_engine::frame_context;
    using rendering_engine::render_graph::frame_graph;
    using rendering_engine::render_graph::pass_io_builder;

    // The graph only forwards the encoder to each pass's callback, so a stub
    // that records nothing is enough to drive execute().
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
    };

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
    frame_graph::execute_fn trace(std::vector<std::string>& log, std::string name)
    {
        return [&log, name = std::move(name)](gpu::command_encoder&, const frame_context&) { log.push_back(name); };
    }

    // The engine's built-in pass list as rendering_engine::context::init
    // registers it (TAA enabled, debug build), with each pass's declare_io
    // transcribed. Kept in step by hand: a pass whose declaration changes
    // should update this mirror too.
    void add_engine_passes(frame_graph& graph)
    {
        graph.import_external("swapchain");
        graph.import_external("taa_history");
        graph.add_pass("shadow", io_of({}, {"shadow_map"}), {});
        graph.add_pass("point_shadow", io_of({}, {"point_shadow"}), {});
        graph.add_pass("scene", io_of({"shadow_map", "point_shadow"}, {"scene_color", "scene_depth"}), {});
        graph.add_pass("skybox", io_of({"scene_color", "scene_depth"}, {"scene_color", "scene_depth"}), {});
        graph.add_pass("velocity", io_of({"scene_depth"}, {"velocity"}), {});
        graph.add_pass("bloom", io_of({"scene_color"}, {"scene_color"}), {});
        graph.add_pass("tonemap", io_of({"scene_color"}, {"ldr_color"}), {});
        graph.add_pass("taa", io_of({"ldr_color", "velocity", "taa_history"}, {"taa_resolve", "taa_history"}), {});
        graph.add_pass("fxaa", io_of({"ldr_color"}, {"swapchain"}), {});
        graph.add_pass("ui", io_of({"swapchain"}, {"swapchain"}), {});
        graph.add_pass("debug", io_of({"swapchain"}, {"swapchain"}), {});
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

// --- compile ----------------------------------------------------------------

TEST(frame_graph, empty_graph_compiles_hazard_free)
{
    frame_graph graph;
    EXPECT_TRUE(graph.compile());
    EXPECT_EQ(graph.pass_count(), 0u);
}

TEST(frame_graph, pass_declaring_nothing_is_hazard_free)
{
    frame_graph graph;
    graph.add_pass("silent", pass_io_builder{}, {});
    EXPECT_TRUE(graph.compile());
}

TEST(frame_graph, read_of_imported_resource_is_produced)
{
    frame_graph graph;
    graph.import_external("swapchain");
    graph.add_pass("ui", io_of({"swapchain"}, {"swapchain"}), {});
    EXPECT_TRUE(graph.compile());
}

TEST(frame_graph, read_of_resource_written_by_earlier_pass_is_produced)
{
    frame_graph graph;
    graph.add_pass("scene", io_of({}, {"scene_color"}), {});
    graph.add_pass("tonemap", io_of({"scene_color"}, {"ldr_color"}), {});
    EXPECT_TRUE(graph.compile());
}

TEST(frame_graph, read_before_any_producer_is_a_hazard)
{
    frame_graph graph;
    graph.add_pass("tonemap", io_of({"scene_color"}, {"ldr_color"}), {});
    EXPECT_FALSE(graph.compile());
}

TEST(frame_graph, read_ahead_of_its_producer_is_a_hazard)
{
    frame_graph graph;
    graph.add_pass("tonemap", io_of({"scene_color"}, {"ldr_color"}), {});
    graph.add_pass("scene", io_of({}, {"scene_color"}), {});
    EXPECT_FALSE(graph.compile());
}

TEST(frame_graph, same_pass_read_and_write_needs_an_earlier_producer)
{
    // A pass's own write does not satisfy its own read: bloom reads and
    // writes scene_color, so something before it must have produced it.
    frame_graph unproduced;
    unproduced.add_pass("bloom", io_of({"scene_color"}, {"scene_color"}), {});
    EXPECT_FALSE(unproduced.compile());

    frame_graph produced;
    produced.add_pass("scene", io_of({}, {"scene_color"}), {});
    produced.add_pass("bloom", io_of({"scene_color"}, {"scene_color"}), {});
    EXPECT_TRUE(produced.compile());
}

TEST(frame_graph, one_hazard_fails_the_whole_compile)
{
    frame_graph graph;
    graph.import_external("swapchain");
    graph.add_pass("scene", io_of({}, {"scene_color"}), {});
    graph.add_pass("tonemap", io_of({"scene_color"}, {"ldr_color"}), {});
    graph.add_pass("fxaa", io_of({"taa_resolve"}, {"swapchain"}), {});
    graph.add_pass("ui", io_of({"swapchain"}, {"swapchain"}), {});
    EXPECT_FALSE(graph.compile());
}

TEST(frame_graph, compile_is_repeatable_and_does_not_execute)
{
    std::vector<std::string> log;
    frame_graph graph;
    graph.add_pass("scene", io_of({}, {"scene_color"}), trace(log, "scene"));
    graph.add_pass("tonemap", io_of({"scene_color"}, {"ldr_color"}), trace(log, "tonemap"));
    EXPECT_TRUE(graph.compile());
    EXPECT_TRUE(graph.compile());
    EXPECT_TRUE(log.empty());
}

// --- execute ----------------------------------------------------------------

TEST(frame_graph, execute_runs_passes_in_registration_order)
{
    std::vector<std::string> log;
    frame_graph graph;
    graph.add_pass("shadow", pass_io_builder{}, trace(log, "shadow"));
    graph.add_pass("scene", pass_io_builder{}, trace(log, "scene"));
    graph.add_pass("ui", pass_io_builder{}, trace(log, "ui"));

    null_encoder encoder;
    frame_context ctx{};
    graph.execute(encoder, ctx);
    EXPECT_EQ(log, (std::vector<std::string>{"shadow", "scene", "ui"}));
}

TEST(frame_graph, execute_forwards_the_same_encoder_and_context_to_every_pass)
{
    frame_graph graph;
    std::vector<const gpu::command_encoder*> encoders;
    std::vector<const frame_context*> contexts;
    std::vector<uint64_t> depth_ids;
    for (int i = 0; i < 3; ++i)
    {
        graph.add_pass("pass",
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
    graph.execute(encoder, ctx);

    ASSERT_EQ(encoders.size(), 3u);
    for (size_t i = 0; i < 3; ++i)
    {
        EXPECT_EQ(encoders[i], &encoder);
        EXPECT_EQ(contexts[i], &ctx);
        EXPECT_EQ(depth_ids[i], 42u);
    }
}

TEST(frame_graph, execute_skips_passes_without_a_callback)
{
    std::vector<std::string> log;
    frame_graph graph;
    graph.add_pass("first", pass_io_builder{}, trace(log, "first"));
    graph.add_pass("silent", pass_io_builder{}, {});
    graph.add_pass("last", pass_io_builder{}, trace(log, "last"));

    null_encoder encoder;
    frame_context ctx{};
    EXPECT_NO_THROW(graph.execute(encoder, ctx));
    EXPECT_EQ(log, (std::vector<std::string>{"first", "last"}));
}

TEST(frame_graph, execute_range_runs_the_half_open_range)
{
    std::vector<std::string> log;
    frame_graph graph;
    graph.add_pass("a", pass_io_builder{}, trace(log, "a"));
    graph.add_pass("b", pass_io_builder{}, trace(log, "b"));
    graph.add_pass("c", pass_io_builder{}, trace(log, "c"));
    graph.add_pass("d", pass_io_builder{}, trace(log, "d"));

    null_encoder encoder;
    frame_context ctx{};
    graph.execute_range(encoder, ctx, 1, 3);
    EXPECT_EQ(log, (std::vector<std::string>{"b", "c"}));
}

TEST(frame_graph, execute_range_clamps_end_to_the_pass_count)
{
    std::vector<std::string> log;
    frame_graph graph;
    graph.add_pass("a", pass_io_builder{}, trace(log, "a"));
    graph.add_pass("b", pass_io_builder{}, trace(log, "b"));

    null_encoder encoder;
    frame_context ctx{};
    graph.execute_range(encoder, ctx, 1, 100);
    EXPECT_EQ(log, (std::vector<std::string>{"b"}));
}

TEST(frame_graph, execute_range_with_an_empty_or_inverted_range_runs_nothing)
{
    std::vector<std::string> log;
    frame_graph graph;
    graph.add_pass("a", pass_io_builder{}, trace(log, "a"));
    graph.add_pass("b", pass_io_builder{}, trace(log, "b"));

    null_encoder encoder;
    frame_context ctx{};
    graph.execute_range(encoder, ctx, 1, 1);
    graph.execute_range(encoder, ctx, 2, 1);
    graph.execute_range(encoder, ctx, 5, 9);
    EXPECT_TRUE(log.empty());
}

// --- clear ------------------------------------------------------------------

TEST(frame_graph, clear_drops_passes_and_imports)
{
    std::vector<std::string> log;
    frame_graph graph;
    graph.import_external("swapchain");
    graph.add_pass("ui", io_of({"swapchain"}, {"swapchain"}), trace(log, "ui"));
    ASSERT_EQ(graph.pass_count(), 1u);
    ASSERT_TRUE(graph.compile());

    graph.clear();
    EXPECT_EQ(graph.pass_count(), 0u);

    null_encoder encoder;
    frame_context ctx{};
    graph.execute(encoder, ctx);
    EXPECT_TRUE(log.empty());

    // The import went too: the same read is now unproduced.
    graph.add_pass("ui", io_of({"swapchain"}, {"swapchain"}), {});
    EXPECT_FALSE(graph.compile());
}

// --- the engine's pass list -------------------------------------------------

TEST(frame_graph, engine_pass_declarations_compile_hazard_free)
{
    frame_graph graph;
    add_engine_passes(graph);
    EXPECT_EQ(graph.pass_count(), 11u);
    EXPECT_TRUE(graph.compile());
}

TEST(frame_graph, scene_depth_must_be_produced_before_the_skybox_and_velocity_read_it)
{
    // Same list, but the scene pass forgets to declare its depth write: the
    // skybox's depth-tested load and the velocity pass's sample both flag.
    frame_graph graph;
    graph.import_external("swapchain");
    graph.import_external("taa_history");
    graph.add_pass("shadow", io_of({}, {"shadow_map"}), {});
    graph.add_pass("point_shadow", io_of({}, {"point_shadow"}), {});
    graph.add_pass("scene", io_of({"shadow_map", "point_shadow"}, {"scene_color"}), {});
    graph.add_pass("skybox", io_of({"scene_color", "scene_depth"}, {"scene_color", "scene_depth"}), {});
    graph.add_pass("velocity", io_of({"scene_depth"}, {"velocity"}), {});
    EXPECT_FALSE(graph.compile());
}

TEST(frame_graph, velocity_ahead_of_the_scene_pass_is_a_hazard)
{
    frame_graph graph;
    graph.import_external("swapchain");
    graph.import_external("taa_history");
    graph.add_pass("velocity", io_of({"scene_depth"}, {"velocity"}), {});
    graph.add_pass("scene", io_of({}, {"scene_color", "scene_depth"}), {});
    EXPECT_FALSE(graph.compile());
}
