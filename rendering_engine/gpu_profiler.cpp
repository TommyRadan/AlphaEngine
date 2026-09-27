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

#include <rendering_engine/gpu_profiler.hpp>

#include <core/log.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine
{
    namespace
    {
        constexpr float nanoseconds_per_millisecond = 1.0e6f;
    } // namespace

    void gpu_profiler::init(gpu::device& device, const std::vector<std::string>& pass_names)
    {
        m_enabled = false;
        m_timings.clear();
        m_timings.reserve(pass_names.size());
        for (const std::string& name : pass_names)
        {
            m_timings.push_back({name, 0.0f});
        }
        m_frame_ms = 0.0f;
        m_write_set = 0;

        // One set per frame in flight plus the one being written: the
        // oldest set of the ring is then always a frame the device has
        // waited for (see the file comment).
        const size_t set_count = static_cast<size_t>(device.frames_in_flight()) + 1u;
        m_sets.assign(set_count, gpu::query_set{});
        m_written.assign(set_count, false);

        if (!device.features().timestamp_queries)
        {
            LOG_INF("gpu_profiler: the device writes no timestamps; per-pass GPU times are off");
            return;
        }
        m_query_count = first_pass_slot + static_cast<uint32_t>(pass_names.size()) * 2u;
        m_timestamp_period_ns = device.limits().timestamp_period_ns;
        gpu::query_set_descriptor descriptor{};
        descriptor.count = m_query_count;
        for (gpu::query_set& set : m_sets)
        {
            set = device.create_query_set(descriptor);
            if (!set.valid())
            {
                shutdown(device);
                return;
            }
        }
        m_ticks.assign(m_query_count, 0);
        m_enabled = true;
    }

    void gpu_profiler::shutdown(gpu::device& device)
    {
        for (gpu::query_set& set : m_sets)
        {
            if (set.valid())
            {
                device.destroy(set);
                set = {};
            }
        }
        m_enabled = false;
        m_written.assign(m_written.size(), false);
    }

    void gpu_profiler::resolve(gpu::device& device)
    {
        if (!m_enabled || m_sets.empty())
        {
            return;
        }
        // The oldest set of the ring, written frames_in_flight frames
        // ago; a set that has not completed yet keeps the previous
        // values on screen.
        const auto set_count = static_cast<uint32_t>(m_sets.size());
        const uint32_t read_set = (m_write_set + 1u) % set_count;
        if (!m_written[read_set])
        {
            return;
        }
        if (!device.resolve_queries(m_sets[read_set], 0, m_query_count, m_ticks.data()))
        {
            return;
        }
        const auto interval_ms = [&](uint32_t begin_slot, uint32_t end_slot)
        {
            const uint64_t begin = m_ticks[begin_slot];
            const uint64_t end = m_ticks[end_slot];
            if (end < begin)
            {
                return 0.0f;
            }
            return static_cast<float>(static_cast<double>(end - begin) * static_cast<double>(m_timestamp_period_ns) /
                                      static_cast<double>(nanoseconds_per_millisecond));
        };
        m_frame_ms = interval_ms(frame_begin_slot, frame_end_slot);
        for (size_t i = 0; i < m_timings.size(); ++i)
        {
            m_timings[i].gpu_ms = interval_ms(pass_begin_slot(i), pass_begin_slot(i) + 1u);
        }
    }

    void gpu_profiler::begin_frame(gpu::command_encoder& encoder)
    {
        if (!m_enabled)
        {
            return;
        }
        // Vulkan needs every query reset before it is written again;
        // recorded at the frame's top, outside any pass.
        encoder.reset_queries(m_sets[m_write_set], 0, m_query_count);
        encoder.write_timestamp(m_sets[m_write_set], frame_begin_slot);
    }

    void gpu_profiler::end_frame(gpu::command_encoder& encoder)
    {
        if (!m_enabled)
        {
            return;
        }
        encoder.write_timestamp(m_sets[m_write_set], frame_end_slot);
        m_written[m_write_set] = true;
        m_write_set = (m_write_set + 1u) % static_cast<uint32_t>(m_sets.size());
    }

    void gpu_profiler::before_pass(gpu::command_encoder& encoder, size_t index, std::string_view /*name*/)
    {
        if (!m_enabled || index >= m_timings.size())
        {
            return;
        }
        encoder.write_timestamp(m_sets[m_write_set], pass_begin_slot(index));
    }

    void gpu_profiler::after_pass(gpu::command_encoder& encoder, size_t index, std::string_view /*name*/)
    {
        if (!m_enabled || index >= m_timings.size())
        {
            return;
        }
        encoder.write_timestamp(m_sets[m_write_set], pass_begin_slot(index) + 1u);
    }
} // namespace rendering_engine
