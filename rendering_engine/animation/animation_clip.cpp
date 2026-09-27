// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/animation/animation_clip.hpp>

#include <algorithm>
#include <utility>

namespace rendering_engine
{
    animation_clip::animation_clip(std::string name, std::vector<joint_track> tracks)
        : m_name{std::move(name)}, m_tracks{std::move(tracks)}
    {
        for (const joint_track& track : m_tracks)
        {
            m_duration =
                std::max({m_duration, track.translation.end_time(), track.rotation.end_time(), track.scale.end_time()});
        }
    }

    const std::string& animation_clip::name() const noexcept
    {
        return m_name;
    }

    const std::vector<joint_track>& animation_clip::tracks() const noexcept
    {
        return m_tracks;
    }

    float animation_clip::duration() const noexcept
    {
        return m_duration;
    }

    bool animation_clip::animates(std::size_t joint) const noexcept
    {
        return std::any_of(
            m_tracks.begin(), m_tracks.end(), [joint](const joint_track& track) { return track.joint == joint; });
    }

    void animation_clip::sample(float time, std::span<core::math::trs> pose) const
    {
        for (const joint_track& track : m_tracks)
        {
            if (track.joint >= pose.size())
            {
                continue;
            }
            core::math::trs& local = pose[track.joint];
            if (!track.translation.empty())
            {
                local.translation = track.translation.sample(time);
            }
            if (!track.rotation.empty())
            {
                local.rotation = track.rotation.sample(time);
            }
            if (!track.scale.empty())
            {
                local.scale = track.scale.sample(time);
            }
        }
    }
} // namespace rendering_engine
