// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file audio_device.hpp
 * @brief The SDL3 implementations of @ref core::audio_output and
 *        @ref core::audio_decoder that @ref core::audio plays and decodes
 *        through.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <core/audio/audio_backend.hpp>

struct SDL_AudioStream;

namespace platform
{
    /**
     * @brief The default playback device, opened through SDL3 as an
     *        @c SDL_AudioStream bound to an @c SDL_OpenAudioDevice handle.
     *
     * The stream's get callback runs on SDL's audio device thread whenever
     * the device needs more to play: it has the render function fill blocks
     * of samples in the mixer's format and puts them into the stream, which
     * converts them to whatever the device runs at. The callback runs under
     * the stream's lock, so @ref close, which clears it under that lock
     * before destroying the stream, returns only once no call is running.
     * @ref open brings SDL's audio subsystem up and @ref close takes it
     * down again.
     */
    struct sdl_audio_output : core::audio_output
    {
        sdl_audio_output() = default;
        ~sdl_audio_output() override;

        sdl_audio_output(const sdl_audio_output&) = delete;
        sdl_audio_output& operator=(const sdl_audio_output&) = delete;
        sdl_audio_output(sdl_audio_output&&) = delete;
        sdl_audio_output& operator=(sdl_audio_output&&) = delete;

        bool
        open(std::uint32_t sample_rate, std::uint32_t channels, render_function render, std::string& error) override;
        void close() override;
        std::string name() const override;

    private:
        // The stream's get callback: fills @p additional_amount bytes, block
        // by block. Runs on SDL's audio device thread.
        static void on_stream_request(void* user, SDL_AudioStream* stream, int additional_amount, int total_amount);

        std::uint32_t m_device{0}; // an SDL_AudioDeviceID; 0 is SDL's "invalid" value
        SDL_AudioStream* m_stream{nullptr};
        std::uint32_t m_channels{0};
        render_function m_render;
        std::vector<float> m_block; // one block of rendered samples, reused by every request
    };

    /**
     * @brief Decodes WAV files with @c SDL_LoadWAV_IO and converts them to
     *        the requested format through a one-shot @c SDL_AudioStream.
     *        Needs no open device.
     */
    struct sdl_audio_decoder : core::audio_decoder
    {
        bool decode_wav(const std::vector<std::byte>& bytes,
                        std::uint32_t sample_rate,
                        std::uint32_t channels,
                        std::vector<float>& samples,
                        std::string& error) override;
    };
} // namespace platform
