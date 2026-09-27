// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file gpu_profiler.hpp
 * @brief Per-pass GPU timings from the device's timestamp queries.
 *
 * The profiler brackets every pass (and the whole frame)
 * with @c command_encoder::write_timestamp through the pass list's
 * @ref pass_hooks. One query set more than the device
 * keeps frames in flight rotate: a frame writes one set and reads back,
 * with @c device::resolve_queries at its top, the set written
 * @c frames_in_flight frames earlier — the frame fence wait in
 * @c begin_frame has retired that frame by then, so the set it is about
 * to reset is complete too. The overlay's profiler panel shows the
 * result.
 * On a device without @c device_features::timestamp_queries the
 * profiler stays disabled and records nothing.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass_list.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct command_encoder;
        struct device;
    } // namespace gpu

    // GPU time of one pass, in milliseconds, from the last
    // frame whose queries resolved.
    struct gpu_pass_timing
    {
        std::string name;
        float gpu_ms{0.0f};
    };

    class gpu_profiler final : public pass_hooks
    {
    public:
        // Create the query sets for @p pass_names passes. No-op, leaving
        // the profiler disabled, when the device writes no timestamps.
        void init(gpu::device& device, const std::vector<std::string>& pass_names);

        // Release the query sets.
        void shutdown(gpu::device& device);

        // Read back the set written frames_in_flight frames ago. Call
        // once per frame after @c device::begin_frame and before
        // @ref begin_frame.
        void resolve(gpu::device& device);

        // Reset this frame's set and stamp the frame start; record it
        // before the pass list records on @p encoder.
        void begin_frame(gpu::command_encoder& encoder);

        // Stamp the frame end and rotate to the next set; record it after
        // the pass list recorded on @p encoder.
        void end_frame(gpu::command_encoder& encoder);

        // pass_hooks — stamps around each pass.
        void before_pass(gpu::command_encoder& encoder, size_t index, std::string_view name) override;
        void after_pass(gpu::command_encoder& encoder, size_t index, std::string_view name) override;

        bool enabled() const noexcept
        {
            return m_enabled;
        }

        // Per-pass timings of the last resolved frame, in pass order.
        const std::vector<gpu_pass_timing>& timings() const noexcept
        {
            return m_timings;
        }

        // GPU time of the last resolved frame, first pass start to last
        // pass end, in milliseconds.
        float frame_gpu_ms() const noexcept
        {
            return m_frame_ms;
        }

    private:
        // Query slots of one set: the frame's begin / end stamps, then
        // two per pass.
        static constexpr uint32_t frame_begin_slot = 0;
        static constexpr uint32_t frame_end_slot = 1;
        static constexpr uint32_t first_pass_slot = 2;

        uint32_t pass_begin_slot(size_t index) const noexcept
        {
            return first_pass_slot + static_cast<uint32_t>(index) * 2u;
        }

        // frames_in_flight + 1 sets, rotated one per frame; the set
        // read at a frame's top is the one written frames_in_flight
        // frames ago, the oldest of the ring.
        std::vector<gpu::query_set> m_sets;
        // Whether each set holds a frame's stamps to resolve.
        std::vector<bool> m_written;
        uint32_t m_write_set{0};
        uint32_t m_query_count{0};
        float m_timestamp_period_ns{1.0f};
        bool m_enabled{false};

        std::vector<uint64_t> m_ticks;
        std::vector<gpu_pass_timing> m_timings;
        float m_frame_ms{0.0f};
    };
} // namespace rendering_engine
