// Unit tests for core::jobs: parallel_for coverage, dispatch + wait_idle
// completion, correctness on workloads that fan out across worker threads,
// the exception boundary around every job, the priority split between
// frame-critical batches and background work, dispatch and parallel_for
// interleaved (from the caller, from inside a batch, and a batch forked from
// a dispatched job), and teardown with work queued or still executing.
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

TEST(jobs, worker_count_leaves_one_hardware_thread_for_the_caller)
{
    core::jobs pool;
    // The documented sizing: hardware_concurrency() - 1 workers, the caller
    // being the last participant, and none at all when the count is 1 or
    // unknown (0). Two pools in one process size themselves the same way.
    const unsigned int hardware = std::thread::hardware_concurrency();
    const unsigned int expected = hardware > 1 ? hardware - 1 : 0;
    EXPECT_EQ(pool.worker_count(), expected);

    core::jobs another;
    EXPECT_EQ(another.worker_count(), expected);
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

// --- dispatch and parallel_for interleaved --------------------------------------

TEST(jobs, dispatch_bursts_interleaved_with_parallel_for_lose_nothing)
{
    core::jobs pool;
    std::atomic<int> background{0};
    std::atomic<int> batch{0};
    const int rounds = 8;
    const int per_round = 16;
    const std::size_t batch_size = 500;

    // Background jobs queued before, and a high-priority one after, each
    // batch: the batch retires with the low queue still full, and every job
    // at either priority is accounted for by the final wait.
    for (int round = 0; round < rounds; ++round)
    {
        for (int i = 0; i < per_round; ++i)
        {
            pool.dispatch([&] { background.fetch_add(1, std::memory_order_relaxed); });
        }
        pool.parallel_for(
            batch_size, [&](std::size_t) { batch.fetch_add(1, std::memory_order_relaxed); }, 8);
        pool.dispatch([&] { background.fetch_add(1, std::memory_order_relaxed); }, core::jobs::priority::high);
    }
    pool.wait_idle();

    EXPECT_EQ(background.load(), rounds * (per_round + 1));
    EXPECT_EQ(batch.load(), static_cast<int>(batch_size) * rounds);
}

TEST(jobs, a_parallel_for_body_may_dispatch_background_work)
{
    core::jobs pool;
    const std::size_t n = 200;
    std::atomic<int> spawned{0};

    // Each index queues a job from whichever thread runs it. parallel_for
    // returns once its own chunks retire — the low-priority jobs they queued
    // are not part of the batch — and wait_idle then collects them.
    pool.parallel_for(
        n, [&](std::size_t) { pool.dispatch([&] { spawned.fetch_add(1, std::memory_order_relaxed); }); }, 4);
    pool.wait_idle();

    EXPECT_EQ(spawned.load(), static_cast<int>(n));
}

TEST(jobs, a_dispatched_job_may_fork_its_own_parallel_for)
{
    core::jobs pool;
    const std::size_t n = 300;
    std::atomic<int> hits{0};
    std::atomic<bool> batch_returned{false};

    // A worker (or the caller, inline) forks a batch from inside a job: it
    // helps run the chunks it queued and blocks on the rest exactly as the
    // main thread would, so the job completes only once the batch has.
    pool.dispatch(
        [&]
        {
            pool.parallel_for(
                n, [&](std::size_t) { hits.fetch_add(1, std::memory_order_relaxed); }, 4);
            batch_returned.store(true, std::memory_order_release);
        });
    pool.wait_idle();

    EXPECT_TRUE(batch_returned.load(std::memory_order_acquire));
    EXPECT_EQ(hits.load(), static_cast<int>(n));
}

TEST(jobs, batches_forked_from_several_jobs_at_once_all_complete)
{
    core::jobs pool;
    const int forks = 4;
    const std::size_t n = 100;
    std::atomic<int> hits{0};

    // Several jobs fork concurrently onto the one shared high-priority
    // queue; a thread helping with its own batch may run another batch's
    // chunks (or another forking job) along the way. Every chunk still
    // retires and nothing waits on work no thread will run.
    for (int i = 0; i < forks; ++i)
    {
        pool.dispatch(
            [&] { pool.parallel_for(n, [&](std::size_t) { hits.fetch_add(1, std::memory_order_relaxed); }, 2); },
            core::jobs::priority::high);
    }
    pool.wait_idle();

    EXPECT_EQ(hits.load(), forks * static_cast<int>(n));
}

// --- teardown -----------------------------------------------------------------

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

TEST(jobs, destructor_waits_for_a_job_that_is_still_executing)
{
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    {
        core::jobs pool;
        pool.dispatch(
            [&]
            {
                started.store(true, std::memory_order_release);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                finished.store(true, std::memory_order_release);
            });
        // With workers, the job is executing on one of them by the time this
        // loop exits (it is no longer merely queued); without, dispatch ran
        // it inline and it has already finished. Either way the pool goes out
        // of scope with the job's captures still referenced.
        while (!started.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
    }
    // Only a destructor that waited for the running job — not just joined
    // an idle queue — can have let it reach this store.
    EXPECT_TRUE(finished.load(std::memory_order_acquire));
}

TEST(jobs, destructor_drains_jobs_queued_behind_a_running_batch)
{
    std::atomic<int> low_done{0};
    std::atomic<int> batch_done{0};
    const int low_jobs = 32;
    const std::size_t n = 400;
    {
        core::jobs pool;
        for (int i = 0; i < low_jobs; ++i)
        {
            pool.dispatch([&] { low_done.fetch_add(1, std::memory_order_relaxed); });
        }
        pool.parallel_for(
            n, [&](std::size_t) { batch_done.fetch_add(1, std::memory_order_relaxed); }, 8);
        // The batch has retired; the background jobs it jumped ahead of may
        // still be queued and are the destructor's to finish.
    }
    EXPECT_EQ(batch_done.load(), static_cast<int>(n));
    EXPECT_EQ(low_done.load(), low_jobs);
}
