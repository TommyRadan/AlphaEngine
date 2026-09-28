// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/scheduler.hpp>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <utility>

#include <core/log.hpp>
#include <core/time.hpp>

namespace
{
    using steady = std::chrono::steady_clock;

    double milliseconds_since(steady::time_point start)
    {
        return std::chrono::duration<double, std::milli>(steady::now() - start).count();
    }

    std::size_t index_of(runtime::stage value) noexcept
    {
        return static_cast<std::size_t>(value);
    }

    // Counts a stage run in progress for as long as it lives.
    struct depth_guard
    {
        explicit depth_guard(int& depth) noexcept : m_depth{&depth}
        {
            ++*m_depth;
        }

        ~depth_guard()
        {
            --*m_depth;
        }

        depth_guard(const depth_guard&) = delete;
        depth_guard& operator=(const depth_guard&) = delete;

    private:
        int* m_depth;
    };

    constexpr const char* k_stage_names[runtime::stage_count] = {
        "input",
        "scripts_fixed",
        "physics",
        "post_physics",
        "update",
        "animation",
        "transform_propagation",
        "audio",
        "render_extract",
    };
} // namespace

namespace runtime
{
    struct scheduler::entry
    {
        std::uint64_t id{0};
        stage where{stage::input};
        std::string name;
        system_function run;
        system_options options;
        bool removed{false};
        // This frame's cost so far.
        double frame_milliseconds{0.0};
        std::uint32_t frame_runs{0};
    };

    const char* stage_name(stage value) noexcept
    {
        const std::size_t index = index_of(value);
        return index < stage_count ? k_stage_names[index] : "unknown";
    }

    bool is_fixed_stage(stage value) noexcept
    {
        return value == stage::scripts_fixed || value == stage::physics || value == stage::post_physics;
    }

    system_registration::system_registration(std::weak_ptr<scheduler*> owner, std::uint64_t id) noexcept
        : m_owner{std::move(owner)}, m_id{id}
    {
    }

    system_registration::~system_registration()
    {
        reset();
    }

    system_registration::system_registration(system_registration&& other) noexcept
        : m_owner{std::move(other.m_owner)}, m_id{std::exchange(other.m_id, 0)}
    {
    }

    system_registration& system_registration::operator=(system_registration&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_owner = std::move(other.m_owner);
            m_id = std::exchange(other.m_id, 0);
        }
        return *this;
    }

    void system_registration::reset()
    {
        if (m_id == 0)
        {
            return;
        }
        if (std::shared_ptr<scheduler*> owner = m_owner.lock())
        {
            (*owner)->remove(m_id);
        }
        m_owner.reset();
        m_id = 0;
    }

    system_registration::operator bool() const noexcept
    {
        return m_id != 0;
    }

    scheduler::scheduler(core::time& clock) : m_clock{&clock}, m_self{std::make_shared<scheduler*>(this)} {}

    scheduler::~scheduler()
    {
        // A registration dropped while the systems below are destroyed (one
        // a system's function held) finds the scheduler already gone.
        m_self.reset();
    }

    system_registration scheduler::add(stage where, std::string name, system_function run, system_options options)
    {
        if (!run || index_of(where) >= stage_count)
        {
            return {};
        }
        auto created = std::make_unique<entry>();
        created->id = m_next_id++;
        created->where = where;
        created->name = std::move(name);
        created->run = std::move(run);
        created->options = options;
        const std::uint64_t id = created->id;
        LOG_DBG("Scheduler: system '%s' added to %s (order %d%s)",
                created->name.c_str(),
                stage_name(where),
                options.order,
                options.while_paused ? ", runs while paused" : "");

        // A stage run in progress keeps its list as it is; the addition
        // waits for it to return.
        if (m_running > 0)
        {
            m_added.push_back(std::move(created));
        }
        else
        {
            insert(std::move(created));
        }
        return system_registration{m_self, id};
    }

    void scheduler::insert(std::unique_ptr<entry> created)
    {
        std::vector<std::unique_ptr<entry>>& systems = m_stages[index_of(created->where)];
        // After every system of the same or a lower order, so equal orders
        // keep the order they were added in.
        const int order = created->options.order;
        const auto position = std::upper_bound(systems.begin(),
                                               systems.end(),
                                               order,
                                               [](int value, const std::unique_ptr<entry>& candidate)
                                               { return value < candidate->options.order; });
        systems.insert(position, std::move(created));
        m_timings_stale = true;
    }

    void scheduler::remove(std::uint64_t id)
    {
        for (std::vector<std::unique_ptr<entry>>& systems : m_stages)
        {
            for (std::unique_ptr<entry>& candidate : systems)
            {
                if (candidate->id == id)
                {
                    candidate->removed = true;
                    m_removed_pending = true;
                }
            }
        }
        for (std::unique_ptr<entry>& candidate : m_added)
        {
            if (candidate->id == id)
            {
                candidate->removed = true;
                m_removed_pending = true;
            }
        }
        if (m_running == 0)
        {
            apply_changes();
        }
    }

    void scheduler::apply_changes()
    {
        // Destroying a removed system's function can drop another system's
        // registration (one it held); with the depth raised that removal only
        // marks its entry, and the next round takes it out.
        const depth_guard applying{m_running};
        while (m_removed_pending || !m_added.empty())
        {
            m_removed_pending = false;
            std::vector<std::unique_ptr<entry>> doomed;
            for (std::vector<std::unique_ptr<entry>>& systems : m_stages)
            {
                const auto kept =
                    std::stable_partition(systems.begin(),
                                          systems.end(),
                                          [](const std::unique_ptr<entry>& candidate) { return !candidate->removed; });
                if (kept != systems.end())
                {
                    doomed.insert(doomed.end(), std::make_move_iterator(kept), std::make_move_iterator(systems.end()));
                    systems.erase(kept, systems.end());
                    m_timings_stale = true;
                }
            }
            std::vector<std::unique_ptr<entry>> added;
            added.swap(m_added);
            for (std::unique_ptr<entry>& created : added)
            {
                if (created->removed)
                {
                    doomed.push_back(std::move(created));
                }
                else
                {
                    insert(std::move(created));
                }
            }
            doomed.clear();
        }
    }

    frame_time scheduler::make_time(double delta) const
    {
        frame_time time;
        time.delta = delta;
        time.unscaled_delta = m_clock->unscaled_delta_time();
        time.time_scale = m_frame_scale;
        time.alpha = m_clock->interpolation_alpha();
        return time;
    }

    void scheduler::run_stage(stage where, const frame_time& time)
    {
        const steady::time_point stage_start = steady::now();
        const bool paused = m_frame_scale <= 0.0;
        {
            // Additions made meanwhile wait in m_added and removals only mark
            // their entry, so the list walked here stays as it is.
            const depth_guard running{m_running};
            std::vector<std::unique_ptr<entry>>& systems = m_stages[index_of(where)];
            for (std::size_t i = 0; i < systems.size(); ++i)
            {
                entry& system = *systems[i];
                if (system.removed || (paused && !system.options.while_paused))
                {
                    continue;
                }
                const steady::time_point start = steady::now();
                system.run(time);
                system.frame_milliseconds += milliseconds_since(start);
                ++system.frame_runs;
            }
        }
        stage_timing& timing = m_frame_stages[index_of(where)];
        timing.milliseconds += milliseconds_since(stage_start);
        ++timing.runs;
        if (m_running == 0)
        {
            apply_changes();
        }
    }

    void scheduler::run_frame()
    {
        // The previous frame, render extraction included, is complete.
        publish_timings();

        // Read once, so every stage of the frame runs at the scale its delta
        // was computed with, whatever a system sets meanwhile.
        m_frame_scale = m_clock->time_scale();
        run_stage(stage::input, make_time(m_clock->delta_time()));

        // The scaled delta feeds the accumulator: a time scale below 1 runs
        // fewer fixed steps, above 1 more, and 0 none. Its clamp bounds the
        // step count (the spiral-of-death guard lives in core::time).
        m_clock->accumulate(m_clock->delta_time());
        m_fixed_steps = 0;
        while (m_clock->next_fixed_step())
        {
            ++m_fixed_steps;
            const frame_time step = make_time(m_clock->fixed_delta_time());
            run_stage(stage::scripts_fixed, step);
            run_stage(stage::physics, step);
            run_stage(stage::post_physics, step);
        }

        const frame_time frame = make_time(m_clock->delta_time());
        run_stage(stage::update, frame);
        run_stage(stage::animation, frame);
        run_stage(stage::transform_propagation, frame);
        run_stage(stage::audio, frame);
    }

    void scheduler::run_render_extract()
    {
        run_stage(stage::render_extract, make_time(m_clock->delta_time()));
    }

    std::uint32_t scheduler::fixed_steps() const noexcept
    {
        return m_fixed_steps;
    }

    const std::array<stage_timing, stage_count>& scheduler::stage_timings() const noexcept
    {
        return m_stage_timings;
    }

    const std::vector<system_timing>& scheduler::system_timings() const noexcept
    {
        return m_system_timings;
    }

    void scheduler::publish_timings()
    {
        m_stage_timings = m_frame_stages;
        m_frame_stages = {};

        // The list keeps its entries (and their names) while the set of
        // systems stays the same; only the figures change frame to frame.
        if (m_timings_stale)
        {
            m_timings_stale = false;
            m_system_timings.clear();
            for (std::size_t index = 0; index < stage_count; ++index)
            {
                for (const std::unique_ptr<entry>& system : m_stages[index])
                {
                    m_system_timings.push_back(system_timing{system->name, static_cast<stage>(index), 0.0, 0});
                }
            }
        }
        std::size_t slot = 0;
        for (std::vector<std::unique_ptr<entry>>& systems : m_stages)
        {
            for (std::unique_ptr<entry>& system : systems)
            {
                if (slot < m_system_timings.size())
                {
                    m_system_timings[slot].milliseconds = system->frame_milliseconds;
                    m_system_timings[slot].runs = system->frame_runs;
                }
                ++slot;
                system->frame_milliseconds = 0.0;
                system->frame_runs = 0;
            }
        }
    }
} // namespace runtime
