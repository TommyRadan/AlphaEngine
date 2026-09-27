// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file audio_types.hpp
 * @brief Lightweight handle and parameter types for @ref core::audio.
 *
 * Split out from audio.hpp (mirroring gpu/handle.hpp and gpu/types.hpp next
 * to gpu/device.hpp) so a caller that only needs to name a voice or fill in
 * playback parameters — such as a component header — does not have to pull
 * in the whole mixer/device declaration.
 */

#pragma once

#include <cstdint>
#include <memory>

#include <core/math/vec3.hpp>

namespace core
{
    struct audio_clip;

    /**
     * @brief Opaque handle to one voice in @ref audio's fixed-size mixer pool.
     *
     * Returned by @ref audio::play and used to control that playback (stop,
     * pause, update its gain / pitch / pan / 3D position) for as long as it
     * is still playing. Mirrors @ref core::pool_handle: a default-constructed
     * handle (@c generation == 0) names no voice, and @c generation guards
     * against a handle outliving the voice slot it named — a finished
     * one-shot recycled by a later @ref audio::play reads as invalid rather
     * than aliasing the new sound. Every @ref audio method that takes one is
     * a safe no-op for an invalid or stale handle, which is what lets the
     * whole subsystem degrade to doing nothing when there is no playback
     * device (see @ref audio::init).
     */
    struct audio_voice_id
    {
        std::uint32_t index{0};
        std::uint32_t generation{0};

        bool valid() const noexcept
        {
            return generation != 0;
        }
    };

    /**
     * @brief Parameters for one @ref audio::play call.
     *
     * A non-spatial voice (@ref spatial false) uses @ref gain and @ref pan
     * directly. A spatial one is mixed against the active listener instead
     * (see @ref audio_listener_component): @ref position, @ref min_distance,
     * @ref max_distance and @ref rolloff feed the distance attenuation and
     * stereo panning computed each mix (@ref audio::update); @ref pan is
     * then unused. A spatial voice mixed with no listener attached plays
     * silently (its playback position still advances) rather than guessing
     * a placement.
     */
    struct audio_play_params
    {
        std::shared_ptr<audio_clip> clip;
        float gain{1.0f};
        float pitch{1.0f}; // playback-rate multiplier: 1 = the clip's original speed and pitch
        bool loop{false};

        float pan{0.0f}; // -1 (left) .. 1 (right); only used when !spatial

        bool spatial{false};
        core::math::vec3 position{};
        float min_distance{1.0f};  // full volume at or inside this distance
        float max_distance{50.0f}; // silent at or beyond this distance
        float rolloff{1.0f};       // shapes the falloff curve between the two distances; 1 is linear
    };
} // namespace core
