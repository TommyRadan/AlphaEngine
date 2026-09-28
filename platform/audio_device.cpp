// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <platform/audio_device.hpp>

#include <algorithm>
#include <utility>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>

namespace platform
{
    namespace
    {
        // Frames rendered per call to the render function; a request for
        // more is filled in several blocks.
        constexpr std::size_t k_block_frames = 1024;

        // "<what> (<SDL's reason>)", the form every failure below reports.
        std::string with_sdl_error(const char* what)
        {
            return std::string{what} + " (" + SDL_GetError() + ")";
        }
    } // namespace

    sdl_audio_output::~sdl_audio_output()
    {
        close();
    }

    bool sdl_audio_output::open(std::uint32_t sample_rate,
                                std::uint32_t channels,
                                render_function render,
                                std::string& error)
    {
        close();

        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            error = with_sdl_error("could not initialize the audio subsystem");
            return false;
        }

        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32;
        spec.channels = static_cast<int>(channels);
        spec.freq = static_cast<int>(sample_rate);

        const SDL_AudioDeviceID device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
        if (device == 0)
        {
            error = with_sdl_error("no playback device available");
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return false;
        }

        m_channels = channels;
        m_render = std::move(render);
        m_block.assign(k_block_frames * channels, 0.0f);

        // The callback goes in before the stream is bound, so the device's
        // first request already reaches the mix.
        SDL_AudioStream* stream = SDL_CreateAudioStream(&spec, &spec);
        if (stream == nullptr || !SDL_SetAudioStreamGetCallback(stream, &sdl_audio_output::on_stream_request, this) ||
            !SDL_BindAudioStream(device, stream))
        {
            error = with_sdl_error("could not create/bind the mixer stream");
            if (stream != nullptr)
            {
                SDL_DestroyAudioStream(stream);
            }
            SDL_CloseAudioDevice(device);
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            m_render = nullptr;
            m_channels = 0;
            return false;
        }

        m_device = device;
        m_stream = stream;
        return true;
    }

    void sdl_audio_output::close()
    {
        if (m_stream == nullptr)
        {
            return;
        }
        // Clearing the callback takes the stream's lock, which a running
        // request holds, so once this returns the render function is done
        // for good.
        SDL_SetAudioStreamGetCallback(m_stream, nullptr, nullptr);
        // Unbinds from the device as part of destroying it; the device
        // itself is only closed by SDL_OpenAudioDeviceStream's stream,
        // which this output does not use (see the class docs).
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
        SDL_CloseAudioDevice(static_cast<SDL_AudioDeviceID>(m_device));
        m_device = 0;
        m_channels = 0;
        m_render = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    std::string sdl_audio_output::name() const
    {
        const char* name = m_device != 0 ? SDL_GetAudioDeviceName(m_device) : nullptr;
        return name != nullptr ? std::string{name} : std::string{};
    }

    void
    sdl_audio_output::on_stream_request(void* user, SDL_AudioStream* stream, int additional_amount, int total_amount)
    {
        (void)total_amount;
        auto& self = *static_cast<sdl_audio_output*>(user);
        if (additional_amount <= 0)
        {
            return;
        }
        // The amount is in bytes of the stream's input format, the mixer's;
        // a partial frame rounds up to a whole one.
        const std::size_t frame_bytes = self.m_channels * sizeof(float);
        std::size_t frames = (static_cast<std::size_t>(additional_amount) + frame_bytes - 1) / frame_bytes;
        while (frames > 0)
        {
            const std::size_t block = std::min(frames, k_block_frames);
            self.m_render(self.m_block.data(), block);
            SDL_PutAudioStreamData(stream, self.m_block.data(), static_cast<int>(block * frame_bytes));
            frames -= block;
        }
    }

    bool sdl_audio_decoder::decode_wav(const std::vector<std::byte>& bytes,
                                       std::uint32_t sample_rate,
                                       std::uint32_t channels,
                                       std::vector<float>& samples,
                                       std::string& error)
    {
        samples.clear();

        SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
        if (io == nullptr)
        {
            error = with_sdl_error("could not wrap the file for decoding");
            return false;
        }

        SDL_AudioSpec file_spec{};
        Uint8* audio_buf = nullptr;
        Uint32 audio_len = 0;
        // closeio = true: SDL_IOFromConstMem's stream is a thin wrapper that
        // does not own bytes, so closing it here frees only that wrapper.
        if (!SDL_LoadWAV_IO(io, true, &file_spec, &audio_buf, &audio_len))
        {
            error = with_sdl_error("not decodable as WAV");
            return false;
        }

        SDL_AudioSpec target_spec{};
        target_spec.format = SDL_AUDIO_F32;
        target_spec.channels = static_cast<int>(channels);
        target_spec.freq = static_cast<int>(sample_rate);

        SDL_AudioStream* convert = SDL_CreateAudioStream(&file_spec, &target_spec);
        if (convert == nullptr)
        {
            error = with_sdl_error("could not set up the format conversion");
            SDL_free(audio_buf);
            return false;
        }

        const bool put_ok = SDL_PutAudioStreamData(convert, audio_buf, static_cast<int>(audio_len));
        SDL_free(audio_buf);
        if (!put_ok || !SDL_FlushAudioStream(convert))
        {
            error = with_sdl_error("the format conversion failed");
            SDL_DestroyAudioStream(convert);
            return false;
        }

        const int available = SDL_GetAudioStreamAvailable(convert);
        if (available > 0)
        {
            samples.resize(static_cast<std::size_t>(available) / sizeof(float));
            const int got = SDL_GetAudioStreamData(convert, samples.data(), available);
            samples.resize(got > 0 ? static_cast<std::size_t>(got) / sizeof(float) : 0);
        }
        SDL_DestroyAudioStream(convert);
        return true;
    }
} // namespace platform
