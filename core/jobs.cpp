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

#include <core/jobs.hpp>

#include <utility>

#include <core/log.hpp>

core::jobs::jobs()
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

    LOG_INF("jobs: worker pool started with %u worker thread(s)", count);
}

core::jobs::~jobs()
{
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

unsigned int core::jobs::worker_count() const noexcept
{
    return static_cast<unsigned int>(m_workers.size());
}

void core::jobs::enqueue(job_fn body)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(std::move(body));
        m_in_flight.fetch_add(1, std::memory_order_relaxed);
    }
    m_wake.notify_one();
}

void core::jobs::execute(job_fn& body)
{
    body();
    // The decrement carries the release so wait_idle's acquire load sees every
    // write the job made. The last one to reach zero wakes any idle waiter.
    if (m_in_flight.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_drained.notify_all();
    }
}

bool core::jobs::run_one_pending()
{
    job_fn body;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty())
        {
            return false;
        }
        body = std::move(m_queue.front());
        m_queue.pop_front();
    }
    execute(body);
    return true;
}

void core::jobs::worker_main()
{
    for (;;)
    {
        job_fn body;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stop || !m_queue.empty(); });
            if (m_stop && m_queue.empty())
            {
                return;
            }
            body = std::move(m_queue.front());
            m_queue.pop_front();
        }
        execute(body);
    }
}

void core::jobs::parallel_for(std::size_t count, const std::function<void(std::size_t)>& body, std::size_t grain)
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
        for (std::size_t i = 0; i < count; ++i)
        {
            body(i);
        }
        return;
    }

    const std::size_t chunks = (count + chunk - 1) / chunk;

    // The batch lives entirely within this call: we block below until every
    // chunk has run, so capturing @p body and @c remaining by reference is safe.
    std::atomic<std::size_t> remaining{chunks};
    for (std::size_t c = 0; c < chunks; ++c)
    {
        const std::size_t begin = c * chunk;
        const std::size_t end = begin + chunk < count ? begin + chunk : count;
        enqueue(
            [&body, &remaining, begin, end]
            {
                for (std::size_t i = begin; i < end; ++i)
                {
                    body(i);
                }
                remaining.fetch_sub(1, std::memory_order_acq_rel);
            });
    }

    // Participate until our own batch is done, helping run whatever is queued.
    while (remaining.load(std::memory_order_acquire) != 0)
    {
        if (!run_one_pending())
        {
            std::this_thread::yield();
        }
    }
}

void core::jobs::dispatch(job_fn body)
{
    // With no workers the job would never be drained, so run it inline.
    if (m_workers.empty())
    {
        body();
        return;
    }
    enqueue(std::move(body));
}

void core::jobs::wait_idle()
{
    // Pitch in first, then sleep until the workers retire the last job.
    while (run_one_pending())
    {
    }
    std::unique_lock<std::mutex> lock(m_mutex);
    m_drained.wait(lock, [this] { return m_in_flight.load(std::memory_order_acquire) == 0; });
}
