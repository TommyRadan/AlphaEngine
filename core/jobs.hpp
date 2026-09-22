/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file jobs.hpp
 * @brief Fixed-size worker pool and the fork-join primitives built on it.
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace core
{
    /**
     * @brief Process-wide worker pool that runs engine work in parallel.
     *
     * Owned by @ref runtime::engine and constructed early so any subsystem can
     * hand it work during init or per frame. The pool starts
     * @c hardware_concurrency() - 1 worker threads — the main thread is the Nth
     * participant. On a single-core host it starts none, and every call below
     * degrades to running inline on the caller, so behaviour is identical minus
     * the parallelism.
     *
     * The model is deliberately fork-join: a caller dispatches a burst of work
     * and blocks (helping run queued jobs meanwhile) until it finishes. There is
     * no free-running background mutation, so the rest of the engine keeps its
     * main-thread-only contract; only the work handed to @ref parallel_for /
     * @ref dispatch ever runs off the main thread. Whatever a job touches must
     * therefore be safe to touch concurrently — read-only shared state, or
     * writes that are disjoint per index.
     *
     * Work is queued at one of two priorities (@ref priority). Frame-critical
     * batches that a caller is blocked on — every @ref parallel_for chunk — run
     * at @c high; fire-and-forget background work handed to @ref dispatch
     * defaults to @c low. Workers, and a caller helping while it waits, always
     * drain the high queue before touching the low one, so a long background
     * job (the asset-streaming use case) can delay a frame-critical batch by at
     * most the jobs already executing when the batch was forked, never by the
     * whole backlog. The flip side is that sustained high-priority load starves
     * low work; that is the intended trade for a game loop.
     *
     * Jobs are expected not to throw. One that does is not fatal: the exception
     * is caught at the job boundary, logged through @c LOG_ERR, and the pool's
     * completion accounting still runs, so @ref wait_idle and @ref parallel_for
     * return normally. It cannot be rethrown — there is no caller on a worker
     * thread — so it is treated as a bug in the job, not a control-flow path.
     *
     * Unlike every other subsystem, this type is itself thread-safe: its
     * queues, dispatch, and completion tracking are synchronised.
     */
    struct jobs
    {
        /** @brief Callable unit of work handed to @ref dispatch. */
        using job_fn = std::function<void()>;

        /** @brief Scheduling class of a queued job; see the class note. */
        enum class priority
        {
            high, /**< Frame-critical: a caller is blocked on it (@ref parallel_for chunks). */
            low,  /**< Background: runs only when no high-priority job is queued (@ref dispatch default). */
        };

        /**
         * @brief Default @c grain for @ref parallel_for.
         *
         * Each chunk costs a heap-allocated @c std::function, a mutex push and
         * a wake; sixty-four indices of typical per-element work (a matrix
         * multiply, a bounds test) amortise that comfortably while still
         * splitting a few-thousand-element range across every worker.
         */
        static constexpr std::size_t default_grain = 64;

        /** @brief Starts the worker threads. */
        jobs();

        /**
         * @brief Drains every queued job, signals the workers to stop, then
         *        joins every one.
         *
         * Equivalent to @ref wait_idle followed by shutdown, so a job still
         * queued or running at teardown finishes before anything it captured
         * (or this pool) goes away.
         */
        ~jobs();

        jobs(const jobs&) = delete;
        jobs& operator=(const jobs&) = delete;
        jobs(jobs&&) = delete;
        jobs& operator=(jobs&&) = delete;

        /**
         * @brief Number of worker threads, excluding the calling/main thread.
         *
         * Zero on a single-core host, in which case every call runs inline.
         */
        unsigned int worker_count() const noexcept;

        /**
         * @brief Runs @p body for every index in <tt>[0, count)</tt> and blocks
         *        until all of them complete.
         *
         * Indices are handed out in contiguous chunks of roughly @p grain,
         * queued at @ref priority::high. The calling thread helps run the
         * high-priority queue until it is empty, then sleeps until the chunks
         * other threads picked up retire — it never spins, and it never takes
         * a low-priority job while its batch is pending. When there are no
         * workers, or the batch is no larger than one chunk, the whole range
         * runs inline on the caller with no threading overhead, so small
         * workloads cost nothing.
         *
         * @param count Number of indices to process.
         * @param body  Invoked as @c body(i) for each @c i. Must be safe to run
         *              concurrently across distinct indices. If it throws, the
         *              exception is logged and the remaining indices of that
         *              chunk are skipped; the call still returns once every
         *              chunk has finished.
         * @param grain Minimum number of indices per chunk. Larger values trade
         *              load balancing for lower per-chunk overhead.
         */
        void parallel_for(std::size_t count,
                          const std::function<void(std::size_t)>& body,
                          std::size_t grain = default_grain);

        /**
         * @brief Enqueues one fire-and-forget job.
         *
         * Returns as soon as the job is queued — or, with no workers, after
         * running it inline. Join with @ref wait_idle. The foundation for
         * background work such as asset streaming, which is why the default
         * priority is @ref priority::low: a queued @ref parallel_for batch is
         * always served first.
         *
         * @param body  The work to run. A throw is logged, not propagated.
         * @param level Queue to place it on.
         */
        void dispatch(job_fn body, priority level = priority::low);

        /**
         * @brief Blocks until every queued job, at either priority, has
         *        finished, helping run queued work meanwhile.
         */
        void wait_idle();

    private:
        // Worker thread entry point: blocks for queued work and runs it.
        void worker_main();
        // Pops and runs one queued job whose priority is at least @p floor,
        // returning false if none was ready.
        bool run_one_pending(priority floor);
        // Moves the next job at priority >= @p floor into @p out. The high
        // queue is always served first. Caller must hold @ref m_mutex.
        bool try_pop_locked(priority floor, job_fn& out);
        // Runs @p body then drops the in-flight count, waking @ref wait_idle at zero.
        void execute(job_fn& body);
        // Queues @p body at @p level and bumps the in-flight count.
        void enqueue(job_fn body, priority level);

        std::vector<std::thread> m_workers;
        std::deque<job_fn> m_high; // parallel_for chunks and explicit high-priority dispatches
        std::deque<job_fn> m_low;  // background dispatches
        std::mutex m_mutex;
        std::condition_variable m_wake;    // a job was queued, or we are stopping
        std::condition_variable m_drained; // the in-flight count reached zero
        std::atomic<std::size_t> m_in_flight{0};
        bool m_stop{false};
    };
} // namespace core
