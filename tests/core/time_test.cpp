// Unit tests for core::time: the fixed-step accounting (the accumulator drains
// whole steps, exposes a fractional interpolation alpha for the remainder, and
// clamps a long frame so it can never queue an unbounded backlog of updates —
// the spiral-of-death guard), constructor validation, and the wall-clock side
// (perform_tick / delta_time / frame_count and the FPS readings). The
// wall-clock tests bracket SDL's performance counter with std::chrono and
// assert bounds only, since exact frame times are not reproducible.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>

#include <core/time.hpp>

namespace
{
    // A 10 ms step keeps the arithmetic exact and easy to read.
    constexpr double k_step = 10.0;
    constexpr int k_max_steps = 5;

    int drain(core::time& clock)
    {
        int steps = 0;
        while (clock.next_fixed_step())
        {
            ++steps;
        }
        return steps;
    }

    // Long enough that any sane counter registers a non-zero frame.
    constexpr auto k_frame_sleep = std::chrono::milliseconds(3);

    void sleep_a_frame()
    {
        std::this_thread::sleep_for(k_frame_sleep);
    }
} // namespace

TEST(time, reports_the_configured_fixed_delta)
{
    core::time clock{k_step, k_max_steps};
    EXPECT_DOUBLE_EQ(clock.fixed_delta_time(), k_step);
}

TEST(time, drains_no_step_below_one_full_delta)
{
    core::time clock{k_step, k_max_steps};
    clock.accumulate(k_step - 0.001);
    EXPECT_EQ(drain(clock), 0);
}

TEST(time, drains_exactly_one_step_per_full_delta)
{
    core::time clock{k_step, k_max_steps};
    clock.accumulate(k_step);
    EXPECT_EQ(drain(clock), 1);
}

TEST(time, drains_multiple_steps_from_a_long_frame)
{
    core::time clock{k_step, k_max_steps};
    clock.accumulate(3 * k_step + 1.0);
    EXPECT_EQ(drain(clock), 3);
}

TEST(time, carries_the_remainder_across_frames)
{
    core::time clock{k_step, k_max_steps};

    // 1.6 steps' worth of time: one step now, 0.6 left over.
    clock.accumulate(1.6 * k_step);
    EXPECT_EQ(drain(clock), 1);
    EXPECT_NEAR(clock.interpolation_alpha(), 0.6, 1e-9);

    // Another 0.6 steps tops the remainder past a full step.
    clock.accumulate(0.6 * k_step);
    EXPECT_EQ(drain(clock), 1);
    EXPECT_NEAR(clock.interpolation_alpha(), 0.2, 1e-9);
}

TEST(time, interpolation_alpha_stays_below_one)
{
    core::time clock{k_step, k_max_steps};
    clock.accumulate(2.5 * k_step);
    drain(clock);
    EXPECT_GE(clock.interpolation_alpha(), 0.0);
    EXPECT_LT(clock.interpolation_alpha(), 1.0);
}

TEST(time, clamps_a_huge_frame_to_max_steps)
{
    core::time clock{k_step, k_max_steps};

    // A 100-step stall must not queue 100 updates — the accumulator is
    // capped at k_max_steps so the loop terminates and the simulation
    // falls behind real time instead of spiralling.
    clock.accumulate(100 * k_step);
    EXPECT_EQ(drain(clock), k_max_steps);
    EXPECT_NEAR(clock.interpolation_alpha(), 0.0, 1e-9);
}

TEST(time, rejects_a_non_positive_fixed_step)
{
    // A zero step would make next_fixed_step loop forever and
    // interpolation_alpha divide by zero.
    EXPECT_THROW((core::time{0.0, k_max_steps}), std::invalid_argument);
    EXPECT_THROW((core::time{-k_step, k_max_steps}), std::invalid_argument);
}

TEST(time, rejects_a_step_cap_below_one)
{
    EXPECT_THROW((core::time{k_step, 0}), std::invalid_argument);
    EXPECT_THROW((core::time{k_step, -1}), std::invalid_argument);
    EXPECT_NO_THROW((core::time{k_step, 1}));
}

TEST(time, starts_at_frame_zero_with_no_delta)
{
    core::time clock{k_step, k_max_steps};
    EXPECT_EQ(clock.frame_count(), 0u);
    EXPECT_DOUBLE_EQ(clock.delta_time(), 0.0);
    EXPECT_FLOAT_EQ(clock.current_fps(), 0.0f);
    EXPECT_FLOAT_EQ(clock.average_fps(), 0.0f);
}

TEST(time, first_tick_reports_frame_one_with_no_delta)
{
    core::time clock{k_step, k_max_steps};
    // Time between construction and the first tick is engine bring-up, not a
    // frame: it must not surface as one giant first delta.
    sleep_a_frame();
    clock.perform_tick();
    EXPECT_EQ(clock.frame_count(), 1u);
    EXPECT_DOUBLE_EQ(clock.delta_time(), 0.0);
}

TEST(time, second_tick_measures_the_elapsed_frame)
{
    core::time clock{k_step, k_max_steps};

    // Bracket both ticks so the delta has a hard upper bound; the sleep
    // between them is its lower bound (the OS never wakes early).
    const auto before = std::chrono::steady_clock::now();
    clock.perform_tick();
    sleep_a_frame();
    clock.perform_tick();
    const double bracket_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
    const double sleep_ms = std::chrono::duration<double, std::milli>(k_frame_sleep).count();

    EXPECT_EQ(clock.frame_count(), 2u);
    EXPECT_GE(clock.delta_time(), sleep_ms - 1.0);
    EXPECT_LE(clock.delta_time(), bracket_ms + 1.0);
    EXPECT_GT(clock.current_fps(), 0.0f);
    EXPECT_GT(clock.average_fps(), 0.0f);
}

TEST(time, instances_keep_independent_clocks)
{
    core::time a{k_step, k_max_steps};
    core::time b{k_step, k_max_steps};

    a.perform_tick();
    sleep_a_frame();
    a.perform_tick();
    // b's first tick must be its own frame one, not a continuation of a's
    // timeline: the previous-tick timestamp used to be a function-local
    // static shared by every instance in the process.
    b.perform_tick();

    EXPECT_GT(a.delta_time(), 0.0);
    EXPECT_EQ(b.frame_count(), 1u);
    EXPECT_DOUBLE_EQ(b.delta_time(), 0.0);
}

TEST(time, average_fps_stays_within_the_instantaneous_range)
{
    core::time clock{k_step, k_max_steps};
    clock.perform_tick();

    float lowest = std::numeric_limits<float>::max();
    float highest = 0.0f;
    for (int i = 0; i < 6; ++i)
    {
        // Vary the frame length so the samples actually differ.
        std::this_thread::sleep_for(std::chrono::milliseconds(1 + i % 3));
        clock.perform_tick();
        lowest = std::min(lowest, clock.current_fps());
        highest = std::max(highest, clock.current_fps());
    }

    // The average is a convex blend of the frame times, so its inverse can
    // never leave the band of instantaneous readings it was built from.
    EXPECT_GT(clock.average_fps(), 0.0f);
    EXPECT_GE(clock.average_fps(), lowest * 0.999f);
    EXPECT_LE(clock.average_fps(), highest * 1.001f);
}
