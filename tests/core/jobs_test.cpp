// Unit tests for core::jobs: parallel_for coverage, dispatch + wait_idle
// completion, correctness on workloads that fan out across worker threads,
// the exception boundary around every job, and the priority split between
// frame-critical batches and background work.
//
// The pool degrades to inline execution on a single-core host, so every
// assertion below holds regardless of worker_count() unless it says otherwise.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>
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

TEST(jobs, a_throwing_job_does_not_wedge_wait_idle)
{
    core::jobs pool;
    std::atomic<int> ran{0};
    pool.dispatch([] { throw std::runtime_error{"job failure"}; });
    pool.dispatch([&] { ran.fetch_add(1, std::memory_order_relaxed); });
    // Without the catch at the job boundary the throwing job would skip the
    // in-flight decrement and this would never return.
    pool.wait_idle();
    EXPECT_EQ(ran.load(), 1);
}

TEST(jobs, a_throwing_job_does_not_escape_the_pool)
{
    core::jobs pool;
    // Covers the inline path too: with no workers dispatch runs the job on the
    // caller, and the exception must still be logged rather than propagated.
    EXPECT_NO_THROW(pool.dispatch([] { throw std::runtime_error{"job failure"}; }));
    EXPECT_NO_THROW(pool.wait_idle());
}

TEST(jobs, a_throwing_parallel_for_body_still_completes_the_batch)
{
    core::jobs pool;
    const std::size_t n = 256;
    std::vector<std::atomic<int>> hits(n);
    for (auto& h : hits)
    {
        h.store(0);
    }

    // The throw sits at the last index so the outcome is the same whether the
    // range runs inline (single core: the loop reaches it last) or as one
    // chunk per index (workers: it is a chunk of its own).
    EXPECT_NO_THROW(pool.parallel_for(
        n,
        [&](std::size_t i)
        {
            if (i == n - 1)
            {
                throw std::runtime_error{"body failure"};
            }
            hits[i].fetch_add(1, std::memory_order_relaxed);
        },
        1));

    for (std::size_t i = 0; i + 1 < n; ++i)
    {
        EXPECT_EQ(hits[i].load(), 1) << "index " << i;
    }
    EXPECT_EQ(hits[n - 1].load(), 0);
}

TEST(jobs, dispatch_accepts_an_explicit_priority)
{
    core::jobs pool;
    std::atomic<int> done{0};
    pool.dispatch([&] { done.fetch_add(1, std::memory_order_relaxed); }, core::jobs::priority::high);
    pool.dispatch([&] { done.fetch_add(1, std::memory_order_relaxed); }, core::jobs::priority::low);
    pool.wait_idle();
    EXPECT_EQ(done.load(), 2);
}

TEST(jobs, low_priority_dispatch_does_not_block_a_parallel_for)
{
    core::jobs pool;
    if (pool.worker_count() == 0)
    {
        GTEST_SKIP() << "no workers: dispatch runs inline, so a blocking job would block the test itself";
    }

    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool gate_open = false;
    auto block_on_gate = [&]
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        gate_cv.wait(lock, [&] { return gate_open; });
    };

    // More blocking background jobs than workers: every worker parks on one
    // and the rest stay queued. The batch below therefore runs on the calling
    // thread alone, which must not pick a queued blocker up along the way —
    // with a single shared queue it would, and hang until the gate opens.
    const unsigned int blockers = pool.worker_count() + 4;
    for (unsigned int i = 0; i < blockers; ++i)
    {
        pool.dispatch(block_on_gate);
    }

    std::atomic<bool> batch_done{false};
    std::atomic<int> hits{0};
    std::thread runner(
        [&]
        {
            pool.parallel_for(
                1000, [&](std::size_t) { hits.fetch_add(1, std::memory_order_relaxed); }, 8);
            batch_done.store(true, std::memory_order_release);
        });

    // The batch must retire with the gate still shut. Bounded so a regression
    // fails the test instead of hanging it.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!batch_done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(batch_done.load(std::memory_order_acquire))
        << "parallel_for waited behind low-priority background jobs";

    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        gate_open = true;
    }
    gate_cv.notify_all();
    runner.join();
    pool.wait_idle();
    EXPECT_EQ(hits.load(), 1000);
}

TEST(jobs, destructor_drains_dispatched_jobs)
{
    std::atomic<int> done{0};
    const int job_count = 64;
    {
        core::jobs pool;
        for (int i = 0; i < job_count; ++i)
        {
            pool.dispatch([&] { done.fetch_add(1, std::memory_order_relaxed); });
        }
        // No wait_idle: the destructor must run every queued job before it
        // joins the workers and the captures above go out of scope.
    }
    EXPECT_EQ(done.load(), job_count);
}
