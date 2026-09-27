// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/job_pool.hpp>

#include <exception>
#include <utility>

#include <core/log.hpp>

namespace
{
    // Runs @p fn, turning any exception it lets escape into a LOG_ERR. Jobs run
    // on worker threads with no caller to rethrow to, and a throw that skipped
    // the pool's completion accounting would leave wait_idle / parallel_for
    // blocked forever — so the "must not throw" contract is enforced at the job
    // boundary rather than trusted.
    template<typename Fn>
    void invoke_logged(Fn&& fn) noexcept
    {
        try
        {
            fn();
        }
        catch (const std::exception& e)
        {
            LOG_ERR("job_pool: job threw an exception: %s", e.what());
        }
        catch (...)
        {
            LOG_ERR("job_pool: job threw a non-standard exception");
        }
    }

    // Completion tracking for one parallel_for batch. Lives on the forking
    // caller's stack for the duration of the call: chunks retire themselves
    // under the mutex and the caller sleeps on @c done until none remain.
    struct batch_state
    {
        std::mutex mutex;
        std::condition_variable done;
        std::size_t remaining{0};
    };
} // namespace

core::job_pool::job_pool()
{
    // Leave one hardware thread for the main thread, which participates in
    // every wait. hardware_concurrency() can report 0 (unknown); treat that —
    // and a genuine single core — as "no workers, run everything inline".
    const unsigned int hardware = std::thread::hardware_concurrency();
    const unsigned int count = hardware > 1 ? hardware - 1 : 0;

    m_workers.reserve(count);
    for (unsigned int i = 0; i < count; ++i)
    {
        m_workers.emplace_back([this] { worker_main(); });
    }

    LOG_INF("job_pool: worker pool started with %u worker thread(s)", count);
}

core::job_pool::~job_pool()
{
    // Drain first: a job still queued or running here may reference engine
    // state that is torn down right after this pool, and joining a worker
    // mid-job would not stop the job, only wait for it — so wait for all of
    // them explicitly, helping from this thread.
    wait_idle();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    for (std::thread& worker : m_workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

unsigned int core::job_pool::worker_count() const noexcept
{
    return static_cast<unsigned int>(m_workers.size());
}

void core::job_pool::enqueue(job_fn body, priority level)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        (level == priority::high ? m_high : m_low).push_back(std::move(body));
        m_in_flight.fetch_add(1, std::memory_order_relaxed);
    }
    m_wake.notify_one();
}

bool core::job_pool::try_pop_locked(priority floor, job_fn& out)
{
    std::deque<job_fn>* queue = nullptr;
    if (!m_high.empty())
    {
        queue = &m_high;
    }
    else if (floor == priority::low && !m_low.empty())
    {
        queue = &m_low;
    }
    if (queue == nullptr)
    {
        return false;
    }
    out = std::move(queue->front());
    queue->pop_front();
    return true;
}

void core::job_pool::execute(job_fn& body)
{
    invoke_logged(body);
    // The decrement carries the release so wait_idle's acquire load sees every
    // write the job made. The last one to reach zero wakes any idle waiter.
    if (m_in_flight.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_drained.notify_all();
    }
}

bool core::job_pool::run_one_pending(priority floor)
{
    job_fn body;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!try_pop_locked(floor, body))
        {
            return false;
        }
    }
    execute(body);
    return true;
}

void core::job_pool::worker_main()
{
    for (;;)
    {
        job_fn body;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stop || !m_high.empty() || !m_low.empty(); });
            if (!try_pop_locked(priority::low, body))
            {
                // Both queues empty, so the wake was the stop signal.
                return;
            }
        }
        execute(body);
    }
}

void core::job_pool::parallel_for(std::size_t count, const std::function<void(std::size_t)>& body, std::size_t grain)
{
    if (count == 0)
    {
        return;
    }

    const std::size_t chunk = grain > 0 ? grain : 1;

    // Inline fast path: nothing to gain from a hand-off when there are no
    // workers, or the whole range fits in a single chunk.
    if (m_workers.empty() || count <= chunk)
    {
        invoke_logged(
            [&]
            {
                for (std::size_t i = 0; i < count; ++i)
                {
                    body(i);
                }
            });
        return;
    }

    const std::size_t chunks = (count + chunk - 1) / chunk;

    // The batch lives entirely within this call: we block below until every
    // chunk has run, so capturing @p body and @c state by reference is safe.
    // Each chunk retires itself under the batch mutex and the last one signals
    // while still holding it, so no chunk touches @c state after the waiter
    // has observed zero and returned.
    batch_state state;
    state.remaining = chunks;
    for (std::size_t c = 0; c < chunks; ++c)
    {
        const std::size_t begin = c * chunk;
        const std::size_t end = begin + chunk < count ? begin + chunk : count;
        enqueue(
            [&body, &state, begin, end]
            {
                invoke_logged(
                    [&]
                    {
                        for (std::size_t i = begin; i < end; ++i)
                        {
                            body(i);
                        }
                    });
                std::lock_guard<std::mutex> lock(state.mutex);
                if (--state.remaining == 0)
                {
                    state.done.notify_all();
                }
            },
            priority::high);
    }

    // Help with the frame-critical queue until it is empty — every chunk of
    // this batch is on it — then sleep until the chunks other threads picked
    // up retire. Low-priority work is never taken here: a long background job
    // would hold this frame hostage.
    while (run_one_pending(priority::high))
    {
    }
    std::unique_lock<std::mutex> lock(state.mutex);
    state.done.wait(lock, [&state] { return state.remaining == 0; });
}

void core::job_pool::dispatch(job_fn body, priority level)
{
    // With no workers the job would never be drained, so run it inline.
    if (m_workers.empty())
    {
        invoke_logged(body);
        return;
    }
    enqueue(std::move(body), level);
}

void core::job_pool::wait_idle()
{
    // Pitch in first, at either priority, then sleep until the workers retire
    // the last job.
    while (run_one_pending(priority::low))
    {
    }
    std::unique_lock<std::mutex> lock(m_mutex);
    m_drained.wait(lock, [this] { return m_in_flight.load(std::memory_order_acquire) == 0; });
}
