// Unit tests for core::logging: the level filter (global and per-category),
// the ALPHAENGINE_LOG_LEVEL specification parser, the bounded ring of recent
// messages, the command-line stash, and the fatal contract (LOG_FTL returns so
// the caller can throw). The stderr / engine.log sinks are exercised only as a
// side effect; what the tests observe is the ring, which is fed by the same
// output callback as the sinks.
//
// LOG_CATEGORY is defined before the first include, the way a translation unit
// tags its messages in engine code.

#define LOG_CATEGORY "log_test"

#include <gtest/gtest.h>

#include <SDL3/SDL_log.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <core/log.hpp>

namespace
{
    using core::logging::record;
    using core::logging::verbosity;

    class log_test : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            // init() installs the SDL output callback that feeds the ring. It is
            // safe to call more than once; do it once per process anyway so the
            // banner does not repeat for every test.
            static const bool initialised = []
            {
                core::logging::init(0, nullptr);
                return true;
            }();
            (void)initialised;

            core::logging::clear_category_levels();
            core::logging::set_level(verbosity::trace);
            core::logging::clear_recent_messages();
        }

        void TearDown() override
        {
            core::logging::clear_category_levels();
            core::logging::set_level(verbosity::info);
            core::logging::clear_recent_messages();
        }
    };

    std::vector<std::string> recent_texts()
    {
        std::vector<std::string> out;
        for (const record& entry : core::logging::recent_messages())
        {
            out.push_back(entry.text);
        }
        return out;
    }
} // namespace

TEST_F(log_test, records_level_category_call_site_and_formatted_text)
{
    const unsigned expected_line = __LINE__; LOG_INF("hello %d %s", 42, "world");

    const auto recent = core::logging::recent_messages();
    ASSERT_EQ(recent.size(), 1u);
    EXPECT_EQ(recent[0].level, verbosity::info);
    EXPECT_EQ(recent[0].category, "log_test");
    EXPECT_EQ(recent[0].file, __FILE__);
    EXPECT_EQ(recent[0].line, expected_line);
    EXPECT_EQ(recent[0].text, "hello 42 world");
}

TEST_F(log_test, every_macro_maps_to_its_level)
{
    LOG_TRC("t");
    LOG_DBG("d");
    LOG_INF("i");
    LOG_WRN("w");
    LOG_ERR("e");
    LOG_FTL("f");

    const auto recent = core::logging::recent_messages();
    ASSERT_EQ(recent.size(), 6u);
    EXPECT_EQ(recent[0].level, verbosity::trace);
    EXPECT_EQ(recent[1].level, verbosity::debug);
    EXPECT_EQ(recent[2].level, verbosity::info);
    EXPECT_EQ(recent[3].level, verbosity::warn);
    EXPECT_EQ(recent[4].level, verbosity::error);
    EXPECT_EQ(recent[5].level, verbosity::fatal);
}

TEST_F(log_test, fatal_returns_so_the_caller_can_throw)
{
    // The documented contract is "LOG_FTL, then throw": the macro must come
    // back so the exception reaches main()'s handler instead of the process
    // aborting inside the log call.
    EXPECT_THROW(
        {
            LOG_FTL("fatal %s", "condition");
            throw std::runtime_error{"fatal condition"};
        },
        std::runtime_error);

    const auto recent = core::logging::recent_messages();
    ASSERT_EQ(recent.size(), 1u);
    EXPECT_EQ(recent[0].level, verbosity::fatal);
    EXPECT_EQ(recent[0].text, "fatal condition");
}

TEST_F(log_test, messages_below_the_global_level_are_dropped)
{
    core::logging::set_level(verbosity::warn);
    EXPECT_EQ(core::logging::level(), verbosity::warn);
    EXPECT_FALSE(core::logging::is_enabled(verbosity::info, "log_test"));
    EXPECT_TRUE(core::logging::is_enabled(verbosity::warn, "log_test"));

    LOG_TRC("trace");
    LOG_DBG("debug");
    LOG_INF("info");
    LOG_WRN("warn");
    LOG_ERR("error");

    const std::vector<std::string> expected{"warn", "error"};
    EXPECT_EQ(recent_texts(), expected);
}

TEST_F(log_test, a_category_override_takes_precedence_over_the_global_level)
{
    core::logging::set_level(verbosity::warn);
    core::logging::set_category_level("gpu", verbosity::trace);

    EXPECT_EQ(core::logging::level_for("gpu"), verbosity::trace);
    EXPECT_EQ(core::logging::level_for("engine"), verbosity::warn);
    EXPECT_EQ(core::logging::level_for(nullptr), verbosity::warn);

    core::logging::message(verbosity::trace, "gpu", __FILE__, __LINE__, "gpu trace");
    core::logging::message(verbosity::trace, "engine", __FILE__, __LINE__, "engine trace");
    core::logging::message(verbosity::error, "engine", __FILE__, __LINE__, "engine error");
    LOG_INF("log_test info");

    const std::vector<std::string> expected{"gpu trace", "engine error"};
    EXPECT_EQ(recent_texts(), expected);

    core::logging::clear_category_levels();
    EXPECT_EQ(core::logging::level_for("gpu"), verbosity::warn);
}

TEST_F(log_test, setting_a_category_level_twice_replaces_the_first)
{
    core::logging::set_category_level("gpu", verbosity::trace);
    core::logging::set_category_level("gpu", verbosity::error);
    EXPECT_EQ(core::logging::level_for("gpu"), verbosity::error);
}

TEST_F(log_test, configure_levels_applies_a_specification)
{
    EXPECT_TRUE(core::logging::configure_levels(" Warn , gpu = TRACE, assets=debug "));
    EXPECT_EQ(core::logging::level(), verbosity::warn);
    EXPECT_EQ(core::logging::level_for("gpu"), verbosity::trace);
    EXPECT_EQ(core::logging::level_for("assets"), verbosity::debug);
    EXPECT_EQ(core::logging::level_for("scene"), verbosity::warn);

    // Overrides alone leave the global level as it is.
    EXPECT_TRUE(core::logging::configure_levels("scene=error"));
    EXPECT_EQ(core::logging::level(), verbosity::warn);
    EXPECT_EQ(core::logging::level_for("scene"), verbosity::error);

    // An empty specification is valid and changes nothing.
    EXPECT_TRUE(core::logging::configure_levels(""));
    EXPECT_EQ(core::logging::level(), verbosity::warn);
}

TEST_F(log_test, configure_levels_rejects_an_invalid_specification_without_changes)
{
    core::logging::set_level(verbosity::error);

    EXPECT_FALSE(core::logging::configure_levels("bogus"));
    EXPECT_FALSE(core::logging::configure_levels("info,gpu=loud"));
    EXPECT_FALSE(core::logging::configure_levels("=debug"));

    EXPECT_EQ(core::logging::level(), verbosity::error);
    EXPECT_EQ(core::logging::level_for("gpu"), verbosity::error);
}

TEST(log_levels, parse_verbosity_accepts_every_name_ignoring_case)
{
    EXPECT_EQ(core::logging::parse_verbosity("trace"), verbosity::trace);
    EXPECT_EQ(core::logging::parse_verbosity("DEBUG"), verbosity::debug);
    EXPECT_EQ(core::logging::parse_verbosity("Info"), verbosity::info);
    EXPECT_EQ(core::logging::parse_verbosity("warn"), verbosity::warn);
    EXPECT_EQ(core::logging::parse_verbosity("warning"), verbosity::warn);
    EXPECT_EQ(core::logging::parse_verbosity("error"), verbosity::error);
    EXPECT_EQ(core::logging::parse_verbosity(" fatal "), verbosity::fatal);

    EXPECT_FALSE(core::logging::parse_verbosity("").has_value());
    EXPECT_FALSE(core::logging::parse_verbosity("verbose").has_value());
    EXPECT_FALSE(core::logging::parse_verbosity("info2").has_value());
}

TEST(log_levels, verbosity_name_round_trips_through_parse_verbosity)
{
    for (const verbosity level : {verbosity::trace,
                                  verbosity::debug,
                                  verbosity::info,
                                  verbosity::warn,
                                  verbosity::error,
                                  verbosity::fatal})
    {
        EXPECT_EQ(core::logging::parse_verbosity(core::logging::verbosity_name(level)), level);
    }
}

TEST_F(log_test, the_ring_keeps_only_the_most_recent_messages_in_order)
{
    constexpr std::size_t k_overflow = 10;
    for (std::size_t i = 0; i < core::logging::k_recent_capacity + k_overflow; ++i)
    {
        LOG_TRC("m%zu", i);
    }

    const auto texts = recent_texts();
    ASSERT_EQ(texts.size(), core::logging::k_recent_capacity);
    EXPECT_EQ(texts.front(), "m" + std::to_string(k_overflow));
    EXPECT_EQ(texts.back(), "m" + std::to_string(core::logging::k_recent_capacity + k_overflow - 1));
    for (std::size_t i = 0; i < texts.size(); ++i)
    {
        EXPECT_EQ(texts[i], "m" + std::to_string(i + k_overflow));
    }
}

TEST_F(log_test, clear_recent_messages_empties_the_ring)
{
    LOG_INF("one");
    LOG_INF("two");
    ASSERT_EQ(core::logging::recent_messages().size(), 2u);

    core::logging::clear_recent_messages();
    EXPECT_TRUE(core::logging::recent_messages().empty());

    LOG_INF("three");
    const std::vector<std::string> expected{"three"};
    EXPECT_EQ(recent_texts(), expected);
}

TEST_F(log_test, messages_from_worker_threads_are_recorded_intact)
{
    constexpr int k_threads = 4;
    constexpr int k_per_thread = 50;

    std::vector<std::thread> workers;
    for (int t = 0; t < k_threads; ++t)
    {
        workers.emplace_back(
            [t]
            {
                for (int i = 0; i < k_per_thread; ++i)
                {
                    LOG_DBG("worker %d message %d", t, i);
                }
            });
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }

    const auto recent = core::logging::recent_messages();
    ASSERT_EQ(recent.size(), static_cast<std::size_t>(k_threads * k_per_thread));
    for (const record& entry : recent)
    {
        EXPECT_EQ(entry.level, verbosity::debug);
        EXPECT_EQ(entry.category, "log_test");
        EXPECT_EQ(entry.text.rfind("worker ", 0), 0u) << entry.text;
        EXPECT_NE(entry.text.find(" message "), std::string::npos) << entry.text;
    }
}

TEST_F(log_test, messages_sdl_emits_itself_are_recorded_under_the_sdl_category)
{
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "from sdl %d", 7);

    const auto recent = core::logging::recent_messages();
    ASSERT_EQ(recent.size(), 1u);
    EXPECT_EQ(recent[0].level, verbosity::warn);
    EXPECT_EQ(recent[0].category, "sdl");
    EXPECT_EQ(recent[0].file, "?");
    EXPECT_EQ(recent[0].line, 0u);
    EXPECT_EQ(recent[0].text, "from sdl 7");
}

TEST_F(log_test, init_stashes_the_command_line)
{
    char program[] = "AlphaEngine";
    char flag[] = "--log-level=trace";
    char* argv[] = {program, flag, nullptr};

    core::logging::init(2, argv);

    const std::vector<std::string> expected{"AlphaEngine", "--log-level=trace"};
    EXPECT_EQ(core::logging::arguments(), expected);
}

TEST_F(log_test, init_tolerates_an_empty_command_line)
{
    core::logging::init(0, nullptr);
    EXPECT_TRUE(core::logging::arguments().empty());
}
