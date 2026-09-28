// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file audio_backend.hpp
 * @brief The platform services @ref core::audio reaches the OS through: a
 *        playback device that pulls mixed samples and a decoder that turns
 *        a sound file into mixer-format samples.
 *
 * core declares them and the platform module implements them
 * (platform/audio_device.hpp); @c runtime::engine hands the
 * implementations to @ref core::audio at construction, so the mixer,
 * clips and voices stay plain C++.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace core
{
    /**
     * @brief A playback device fed with interleaved 32-bit float samples.
     *
     * The device pulls: whenever it needs more to play, it calls the render
     * function it was opened with, on a thread of its own, one call at a
     * time.
     */
    struct audio_output
    {
        /**
         * @brief Fills @p samples with the next @p frame_count frames
         *        (@p frame_count times the open channel count floats). Runs on
         *        the device's thread, so it must not block.
         */
        using render_function = std::function<void(float* samples, std::size_t frame_count)>;

        virtual ~audio_output() = default;

        /**
         * @brief Opens the default playback device for @p channels
         *        interleaved channels at @p sample_rate and starts pulling
         *        from @p render. A device whose native format differs
         *        converts on its side.
         * @param error Receives a one-line reason when no device can be opened.
         * @return true when the device is open; @p render may be called
         *         from then until @ref close returns.
         */
        virtual bool
        open(std::uint32_t sample_rate, std::uint32_t channels, render_function render, std::string& error) = 0;

        /**
         * @brief Closes the device, dropping anything not yet played. Once
         *        it returns the render function is not running and is never
         *        called again. No-op when nothing is open.
         */
        virtual void close() = 0;

        /** @brief A human-readable name of the open device. */
        virtual std::string name() const = 0;
    };

    /** @brief Decodes whole sound files, already read into memory, into mixer-format samples. */
    struct audio_decoder
    {
        virtual ~audio_decoder() = default;

        /**
         * @brief Decodes @p bytes as a WAV file and converts the result to
         *        @p channels interleaved float channels at @p sample_rate.
         * @param samples Receives the converted samples; left empty on failure.
         * @param error   Receives a one-line reason on failure.
         * @return false when @p bytes cannot be decoded or converted.
         */
        virtual bool decode_wav(const std::vector<std::byte>& bytes,
                                std::uint32_t sample_rate,
                                std::uint32_t channels,
                                std::vector<float>& samples,
                                std::string& error) = 0;
    };
} // namespace core
