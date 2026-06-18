// Unit tests for the fixed-step accounting in core::time: the accumulator
// drains whole steps, exposes a fractional interpolation alpha for the
// remainder, and clamps a long frame so it can never queue an unbounded
// backlog of updates (the spiral-of-death guard).
//
// Only the fixed-step methods are exercised here; the wall-clock side
// (perform_tick / total_time) reads SDL's performance counter and is not
// deterministic enough for a unit test.

#include <gtest/gtest.h>

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
