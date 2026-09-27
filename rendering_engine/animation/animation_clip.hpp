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
 * @file animation_clip.hpp
 * @brief The animation clip asset: per-joint translation / rotation / scale
 *        tracks, each a keyframed curve.
 */

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include <core/math/curve.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/animation/skeleton.hpp>

namespace rendering_engine
{
    /**
     * @brief The keyframed channels of one joint. A channel with no keys is
     *        not animated: sampling leaves that part of the pose alone.
     */
    struct joint_track
    {
        // The @ref skeleton joint the track drives.
        std::size_t joint{no_joint};

        core::math::curve<core::math::vec3> translation;
        core::math::curve<core::math::quat> rotation;
        core::math::curve<core::math::vec3> scale;
    };

    /**
     * @brief A named, immutable piece of animation over a @ref skeleton,
     *        shared (as a @c shared_ptr<const animation_clip>) by every
     *        animator that plays it.
     *
     * Time is in seconds from the start of the clip (glTF's unit). The
     * clip's @ref duration is its latest key; sampling past it holds the
     * last keys, and looping is the player's business.
     */
    struct animation_clip
    {
        animation_clip() = default;
        animation_clip(std::string name, std::vector<joint_track> tracks);

        const std::string& name() const noexcept;
        const std::vector<joint_track>& tracks() const noexcept;

        /** @brief The time of the latest key on any channel, in seconds. */
        float duration() const noexcept;

        /** @brief Whether some track of this clip drives @p joint. */
        bool animates(std::size_t joint) const noexcept;

        /**
         * @brief Writes the animated channels' values at @p time into
         *        @p pose (a local pose, index-aligned with the skeleton's
         *        joints).
         *
         * Joints and channels the clip does not animate keep whatever
         * @p pose held — start from the bind pose to get a full pose. A
         * track whose joint lies past the end of @p pose is skipped.
         */
        void sample(float time, std::span<core::math::trs> pose) const;

    private:
        std::string m_name;
        std::vector<joint_track> m_tracks;
        float m_duration{0.0f};
    };
} // namespace rendering_engine
