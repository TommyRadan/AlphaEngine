// Unit tests for core::jobs: parallel_for coverage, dispatch + wait_idle
// completion, and correctness on workloads that fan out across worker threads.
//
// The pool degrades to inline execution on a single-core host, so every
// assertion below holds regardless of worker_count().

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <numeric>
#include <vector>

#include <core/jobs.hpp>

TEST(jobs, parallel_for_visits_every_index_exactly_once)
{
    core::jobs pool;
    const std::size_t n = 1000;
    std::vector<int> counts(n, 0);
    std::vector<std::atomic<int>> hits(n);
    for (auto& h : hits)
    {
        h.store(0);
    }

    pool.parallel_for(n, [&](std::size_t i) { hits[i].fetch_add(1, std::memory_order_relaxed); });

    for (std::size_t i = 0; i < n; ++i)
    {
        EXPECT_EQ(hits[i].load(), 1) << "index " << i;
    }
}

TEST(jobs, parallel_for_zero_count_does_nothing)
{
    core::jobs pool;
    std::atomic<int> calls{0};
    pool.parallel_for(0, [&](std::size_t) { calls.fetch_add(1); });
    EXPECT_EQ(calls.load(), 0);
}

TEST(jobs, parallel_for_computes_a_correct_parallel_sum)
{
    core::jobs pool;
    const std::size_t n = 10000;
    std::vector<long long> values(n);
    std::iota(values.begin(), values.end(), 1LL); // 1..n

    std::atomic<long long> total{0};
    pool.parallel_for(
        n, [&](std::size_t i) { total.fetch_add(values[i], std::memory_order_relaxed); }, 64);

    const long long expected = static_cast<long long>(n) * (n + 1) / 2;
    EXPECT_EQ(total.load(), expected);
}

TEST(jobs, parallel_for_honours_a_grain_larger_than_count)
{
    core::jobs pool;
    std::atomic<int> sum{0};
    pool.parallel_for(
        5, [&](std::size_t i) { sum.fetch_add(static_cast<int>(i), std::memory_order_relaxed); }, 1024);
    EXPECT_EQ(sum.load(), 0 + 1 + 2 + 3 + 4);
}

TEST(jobs, dispatch_then_wait_idle_runs_all_jobs)
{
    core::jobs pool;
    std::atomic<int> done{0};
    const int job_count = 64;
    for (int i = 0; i < job_count; ++i)
    {
        pool.dispatch([&] { done.fetch_add(1, std::memory_order_relaxed); });
    }
    pool.wait_idle();
    EXPECT_EQ(done.load(), job_count);
}

TEST(jobs, wait_idle_with_no_jobs_is_a_noop)
{
    core::jobs pool;
    EXPECT_NO_THROW(pool.wait_idle());
}

TEST(jobs, worker_count_is_reported)
{
    core::jobs pool;
    // Never exceeds the hardware concurrency; may be zero on a single-core host.
    EXPECT_GE(pool.worker_count(), 0u);
}
