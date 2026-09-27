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
