// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file curve.hpp
 * @brief Keyframed curves over the engine's value types, with glTF's step /
 *        linear / cubic-spline interpolation modes, and the value-type
 *        interpolation they (and @c core::tween) are built on.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <core/math/easing.hpp>
#include <core/math/math.hpp>
#include <core/math/trs.hpp>

namespace core::math
{
    /**
     * @brief Blends @p a (t = 0) towards @p b (t = 1) the way the value type
     *        blends: component-wise for scalars and vectors (@c lerp), along
     *        the shortest arc for rotations (@ref slerp), part by part for a
     *        @ref trs pose.
     */
    template<typename T>
    T interpolate(const T& a, const T& b, float t) noexcept
    {
        return lerp(a, b, t);
    }

    inline quat interpolate(const quat& a, const quat& b, float t) noexcept
    {
        return slerp(a, b, t);
    }

    /**
     * @brief The additive identity of @c T, the value a missing tangent
     *        means. A value-initialised vector is zero, but a
     *        value-initialised @ref quat is the identity rotation, so it has
     *        its own overload.
     */
    template<typename T>
    T zero_value() noexcept
    {
        return T{};
    }

    template<>
    inline quat zero_value<quat>() noexcept
    {
        return quat{0.0f, 0.0f, 0.0f, 0.0f};
    }

    /**
     * @brief How a @ref curve fills the gap between two keyframes. The
     *        values and their meaning are glTF's animation sampler modes.
     */
    enum class interpolation : uint8_t
    {
        // Hold each key's value until the next key.
        step,
        // Blend the two keys (@ref interpolate), optionally shaped by the
        // starting key's @ref keyframe::ease.
        linear,
        // Cubic Hermite spline through the keys, with each key's in / out
        // tangents (glTF CUBICSPLINE). A rotation curve renormalises the
        // result.
        cubic_spline,
    };

    /** @brief One key of a @ref curve. */
    template<typename T>
    struct keyframe
    {
        // Position of the key on the curve's axis (seconds, for an animation).
        float time{0.0f};
        T value{};

        // Hermite tangents, read by @ref interpolation::cubic_spline only.
        // In glTF's convention they are rates of change per unit of @c time:
        // the spline scales them by the segment's length.
        T in_tangent{zero_value<T>()};
        T out_tangent{zero_value<T>()};

        // Easing applied to the segment that starts at this key, read by
        // @ref interpolation::linear only; @c linear leaves it untouched.
        easing ease{easing::linear};
    };

    /**
     * @brief A value that varies along one axis, defined by keyframes.
     *
     * The keys must be sorted by ascending @ref keyframe::time. Sampling
     * before the first key returns the first value and after the last key
     * the last value (the curve clamps rather than extrapolates); an empty
     * curve samples to @c T{} and a one-key curve is constant. Works for
     * @c float, @ref vec2, @ref vec3, @ref vec4 and @ref quat.
     */
    template<typename T>
    struct curve
    {
        interpolation mode{interpolation::linear};
        std::vector<keyframe<T>> keys;

        bool empty() const noexcept
        {
            return keys.empty();
        }

        float start_time() const noexcept
        {
            return keys.empty() ? 0.0f : keys.front().time;
        }

        float end_time() const noexcept
        {
            return keys.empty() ? 0.0f : keys.back().time;
        }

        /** @brief The curve's value at @p time. */
        T sample(float time) const
        {
            if (keys.empty())
            {
                return T{};
            }
            if (keys.size() == 1 || time <= keys.front().time)
            {
                return keys.front().value;
            }
            if (time >= keys.back().time)
            {
                return keys.back().value;
            }

            // The first key strictly after @p time closes the segment; the
            // clamps above guarantee one exists and that it is not the first.
            const auto next = std::upper_bound(
                keys.begin(), keys.end(), time, [](float t, const keyframe<T>& key) { return t < key.time; });
            const keyframe<T>& k1 = *next;
            const keyframe<T>& k0 = *(next - 1);
            const float span = k1.time - k0.time;
            if (span <= 0.0f)
            {
                return k1.value;
            }
            const float u = (time - k0.time) / span;

            switch (mode)
            {
            case interpolation::step:
                return k0.value;
            case interpolation::linear:
                return interpolate(k0.value, k1.value, ease(k0.ease, u));
            case interpolation::cubic_spline:
                return hermite(k0.value, k0.out_tangent * span, k1.value, k1.in_tangent * span, u);
            }
            return k0.value;
        }

    private:
        // Cubic Hermite basis: p(u) = h00 p0 + h10 m0 + h01 p1 + h11 m1, with
        // the tangents already scaled by the segment length (glTF 2.0,
        // Appendix C).
        static T hermite(const T& p0, const T& m0, const T& p1, const T& m1, float u)
        {
            const float u2 = u * u;
            const float u3 = u2 * u;
            const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
            const float h10 = u3 - 2.0f * u2 + u;
            const float h01 = -2.0f * u3 + 3.0f * u2;
            const float h11 = u3 - u2;
            T result = p0 * h00 + m0 * h10 + p1 * h01 + m1 * h11;
            if constexpr (std::is_same_v<T, quat>)
            {
                // A spline through unit quaternions leaves the unit sphere
                // between the keys; glTF has the result renormalised.
                result = normalize(result);
            }
            return result;
        }
    };
} // namespace core::math
