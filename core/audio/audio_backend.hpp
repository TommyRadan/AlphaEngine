// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file audio_backend.hpp
 * @brief The platform services @ref core::audio reaches the OS through: a
 *        playback device to queue mixed samples into and a decoder that
 *        turns a sound file into mixer-format samples.
 *
 * core declares them and the platform module implements them
 * (platform/audio_device.hpp); @c runtime::engine hands the
 * implementations to @ref core::audio at construction, so the mixer,
 * clips and voices stay plain C++.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace core
{
    /**
     * @brief A playback device fed with interleaved 32-bit float samples.
     *
     * The mixer pulls: once per tick it asks how much is still queued and
     * tops the queue up. Nothing here runs a callback on another thread.
     */
    struct audio_output
    {
        virtual ~audio_output() = default;

        /**
         * @brief Opens the default playback device for @p channels
         *        interleaved channels at @p sample_rate. A device whose
         *        native format differs converts on its side.
         * @param error Receives a one-line reason when no device can be opened.
         * @return true when the device is open and accepts @ref queue.
         */
        virtual bool open(std::uint32_t sample_rate, std::uint32_t channels, std::string& error) = 0;

        /** @brief Closes the device, dropping anything still queued. No-op when nothing is open. */
        virtual void close() = 0;

        /** @brief A human-readable name of the open device. */
        virtual std::string name() const = 0;

        /** @brief Frames queued but not yet played. */
        virtual std::size_t queued_frames() const = 0;

        /** @brief Appends @p frame_count frames (@p frame_count times the open channel count floats) to the queue. */
        virtual void queue(const float* samples, std::size_t frame_count) = 0;
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
