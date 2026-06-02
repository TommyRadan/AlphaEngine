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
     * Unlike every other subsystem, this type is itself thread-safe: its queue,
     * dispatch, and completion tracking are synchronised.
     */
    struct jobs
    {
        /** @brief Callable unit of work handed to @ref dispatch. */
        using job_fn = std::function<void()>;

        /** @brief Starts the worker threads. */
        jobs();

        /** @brief Signals the workers to stop, then joins every one. */
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
         * Indices are handed out in contiguous chunks of roughly @p grain. The
         * calling thread participates in execution rather than spinning idle.
         * When there are no workers, or the batch is no larger than one chunk,
         * the whole range runs inline on the caller with no threading overhead,
         * so small workloads cost nothing.
         *
         * @param count Number of indices to process.
         * @param body  Invoked as @c body(i) for each @c i. Must be safe to run
         *              concurrently across distinct indices, and must not throw.
         * @param grain Minimum number of indices per chunk. Larger values trade
         *              load balancing for lower per-chunk overhead.
         */
        void parallel_for(std::size_t count, const std::function<void(std::size_t)>& body, std::size_t grain = 1);

        /**
         * @brief Enqueues one fire-and-forget job.
         *
         * Returns as soon as the job is queued — or, with no workers, after
         * running it inline. Join with @ref wait_idle. The foundation for
         * background work such as asset streaming.
         */
        void dispatch(job_fn body);

        /**
         * @brief Blocks until every dispatched job has finished, helping run
         *        queued work meanwhile.
         */
        void wait_idle();

    private:
        // Worker thread entry point: blocks for queued work and runs it.
        void worker_main();
        // Pops and runs a single queued job, returning false if none was ready.
        bool run_one_pending();
        // Runs @p body then drops the in-flight count, waking @ref wait_idle at zero.
        void execute(job_fn& body);
        // Queues @p body and bumps the in-flight count.
        void enqueue(job_fn body);

        std::vector<std::thread> m_workers;
        std::deque<job_fn> m_queue;
        std::mutex m_mutex;
        std::condition_variable m_wake;    // a job was queued, or we are stopping
        std::condition_variable m_drained; // the in-flight count reached zero
        std::atomic<std::size_t> m_in_flight{0};
        bool m_stop{false};
    };
} // namespace core
