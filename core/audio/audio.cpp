// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#define LOG_CATEGORY "audio"
#include <core/audio/audio.hpp>

#include <algorithm>
#include <cmath>

#include <core/audio/audio_clip.hpp>
#include <core/log.hpp>
#include <core/math/math.hpp>
#include <core/platform/platform.hpp>
#include <core/vfs/vfs.hpp>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>

// The one SDL-including translation unit for this subsystem (mirrors
// core/platform/platform_sdl.cpp): everything else under core/audio names
// only the forward-declared SDL_AudioStream and plain core types.

namespace core
{
    namespace
    {
        // A source shorter than this carries no direction; treat the
        // listener and the voice as coincident rather than divide by ~0.
        constexpr float degenerate_length = 1e-6f;

        // Simple linear pan law: full signal to one side at |pan| == 1,
        // equal signal to both at pan == 0. Not equal-power, but this
        // subsystem keeps its DSP deliberately minimal.
        void pan_gains(float gain, float pan, float& left, float& right) noexcept
        {
            const float p = std::clamp(pan, -1.0f, 1.0f);
            left = gain * std::clamp(1.0f - p, 0.0f, 1.0f);
            right = gain * std::clamp(1.0f + p, 0.0f, 1.0f);
        }
    } // namespace

    audio::audio() = default;

    audio::~audio()
    {
        quit();
    }

    void audio::init()
    {
        LOG_INF("Init core::audio");

        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            LOG_WRN("core::audio: could not initialize the audio subsystem (%s); audio is disabled", SDL_GetError());
            return;
        }

        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32;
        spec.channels = static_cast<int>(k_mixer_channels);
        spec.freq = static_cast<int>(k_mixer_sample_rate);

        const SDL_AudioDeviceID device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
        if (device == 0)
        {
            LOG_WRN("core::audio: no playback device available (%s); audio is disabled", SDL_GetError());
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return;
        }

        SDL_AudioStream* stream = SDL_CreateAudioStream(&spec, &spec);
        if (stream == nullptr || !SDL_BindAudioStream(device, stream))
        {
            LOG_WRN("core::audio: could not create/bind the mixer stream (%s); audio is disabled", SDL_GetError());
            if (stream != nullptr)
            {
                SDL_DestroyAudioStream(stream);
            }
            SDL_CloseAudioDevice(device);
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return;
        }

        m_device = device;
        m_stream = stream;
        m_available = true;
        LOG_INF("core::audio: opened '%s' (%u Hz, %u ch)",
                SDL_GetAudioDeviceName(device),
                k_mixer_sample_rate,
                k_mixer_channels);
    }

    void audio::quit()
    {
        // engine::quit() calls this explicitly and the destructor calls it
        // again as a safety net (see ~audio); only log once there is
        // something left to close, so the ordinary shutdown path prints one
        // line rather than two.
        if (m_stream != nullptr || m_available)
        {
            LOG_INF("Quit core::audio");
        }

        if (m_stream != nullptr)
        {
            // Unbinds from the device as part of destroying it; the device
            // itself is only closed by SDL_OpenAudioDeviceStream's stream,
            // which this subsystem does not use (see the class docs).
            SDL_DestroyAudioStream(m_stream);
            m_stream = nullptr;
        }
        if (m_device != 0)
        {
            SDL_CloseAudioDevice(static_cast<SDL_AudioDeviceID>(m_device));
            m_device = 0;
        }
        if (m_available)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        m_available = false;

        for (voice& v : m_voices)
        {
            free_voice(v);
        }
        m_listeners.clear();
        m_clips.clear();
    }

    std::shared_ptr<audio_clip> audio::decode_wav(const std::vector<std::byte>& bytes, const std::string& label)
    {
        SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
        if (io == nullptr)
        {
            LOG_ERR("core::audio: could not wrap '%s' for decoding: %s", label.c_str(), SDL_GetError());
            return nullptr;
        }

        SDL_AudioSpec file_spec{};
        Uint8* audio_buf = nullptr;
        Uint32 audio_len = 0;
        // closeio = true: SDL_IOFromConstMem's stream is a thin wrapper that
        // does not own bytes, so closing it here frees only that wrapper.
        if (!SDL_LoadWAV_IO(io, true, &file_spec, &audio_buf, &audio_len))
        {
            LOG_ERR("core::audio: could not decode '%s' as WAV: %s", label.c_str(), SDL_GetError());
            return nullptr;
        }

        SDL_AudioSpec mixer_spec{};
        mixer_spec.format = SDL_AUDIO_F32;
        mixer_spec.channels = static_cast<int>(k_mixer_channels);
        mixer_spec.freq = static_cast<int>(k_mixer_sample_rate);

        SDL_AudioStream* convert = SDL_CreateAudioStream(&file_spec, &mixer_spec);
        if (convert == nullptr)
        {
            LOG_ERR("core::audio: could not set up format conversion for '%s': %s", label.c_str(), SDL_GetError());
            SDL_free(audio_buf);
            return nullptr;
        }

        const bool put_ok = SDL_PutAudioStreamData(convert, audio_buf, static_cast<int>(audio_len));
        SDL_free(audio_buf);
        if (!put_ok || !SDL_FlushAudioStream(convert))
        {
            LOG_ERR("core::audio: format conversion failed for '%s': %s", label.c_str(), SDL_GetError());
            SDL_DestroyAudioStream(convert);
            return nullptr;
        }

        auto clip = std::make_shared<audio_clip>();
        const int available = SDL_GetAudioStreamAvailable(convert);
        if (available > 0)
        {
            clip->samples.resize(static_cast<std::size_t>(available) / sizeof(float));
            const int got = SDL_GetAudioStreamData(convert, clip->samples.data(), available);
            clip->samples.resize(got > 0 ? static_cast<std::size_t>(got) / sizeof(float) : 0);
        }
        SDL_DestroyAudioStream(convert);

        clip->frame_count = static_cast<std::uint32_t>(clip->samples.size() / k_mixer_channels);
        if (clip->frame_count == 0)
        {
            LOG_ERR("core::audio: '%s' decoded to no audio data", label.c_str());
            return nullptr;
        }

        LOG_DBG("core::audio: loaded '%s' (%u frames @ %u Hz)", label.c_str(), clip->frame_count, k_mixer_sample_rate);
        return clip;
    }

    std::shared_ptr<audio_clip> audio::load_clip(const std::filesystem::path& path)
    {
        const std::string key = core::default_vfs().canonical_key(path);
        if (auto it = m_clips.find(key); it != m_clips.end())
        {
            if (std::shared_ptr<audio_clip> existing = it->second.lock())
            {
                return existing;
            }
        }

        std::vector<std::byte> bytes;
        std::string error;
        if (!core::default_vfs().read_file(path, bytes, &error))
        {
            LOG_ERR("core::audio: could not read '%s': %s", core::platform::path_to_utf8(path).c_str(), error.c_str());
            return nullptr;
        }

        std::shared_ptr<audio_clip> clip = decode_wav(bytes, core::platform::path_to_utf8(path));
        if (clip == nullptr)
        {
            return nullptr;
        }

        clip->key = key;
        m_clips[key] = clip;
        return clip;
    }

    std::size_t audio::find_free_voice() const noexcept
    {
        for (std::size_t i = 0; i < m_voices.size(); ++i)
        {
            if (!m_voices[i].active)
            {
                return i;
            }
        }
        return m_voices.size();
    }

    audio_voice_id audio::play(const audio_play_params& params)
    {
        if (params.clip == nullptr || params.clip->frame_count == 0)
        {
            return {};
        }

        const std::size_t slot = find_free_voice();
        if (slot == m_voices.size())
        {
            if (!m_voice_cap_warned)
            {
                LOG_WRN("core::audio: voice cap (%zu) reached; dropping a playback request", k_max_voices);
                m_voice_cap_warned = true;
            }
            return {};
        }

        voice& v = m_voices[slot];
        v.active = true;
        v.clip = params.clip;
        v.cursor = 0.0;
        v.paused = false;
        v.gain = params.gain;
        v.pitch = params.pitch > 0.0f ? params.pitch : 1.0f;
        v.pan = params.pan;
        v.loop = params.loop;
        v.spatial = params.spatial;
        v.position = params.position;
        v.min_distance = std::max(params.min_distance, 0.0f);
        v.max_distance = std::max(params.max_distance, v.min_distance + 1e-3f);
        v.rolloff = std::max(params.rolloff, 0.0f);
        return audio_voice_id{static_cast<std::uint32_t>(slot), v.generation};
    }

    void audio::play_one_shot(const std::shared_ptr<audio_clip>& clip, float gain, float pan)
    {
        audio_play_params params;
        params.clip = clip;
        params.gain = gain;
        params.pan = pan;
        play(params);
    }

    audio::voice* audio::resolve(audio_voice_id id) noexcept
    {
        if (!id.valid() || id.index >= m_voices.size())
        {
            return nullptr;
        }
        voice& v = m_voices[id.index];
        return (v.active && v.generation == id.generation) ? &v : nullptr;
    }

    const audio::voice* audio::resolve(audio_voice_id id) const noexcept
    {
        if (!id.valid() || id.index >= m_voices.size())
        {
            return nullptr;
        }
        const voice& v = m_voices[id.index];
        return (v.active && v.generation == id.generation) ? &v : nullptr;
    }

    void audio::free_voice(voice& v) noexcept
    {
        v.active = false;
        v.paused = false;
        v.clip.reset();
        if (++v.generation == 0)
        {
            // Skip the invalid handle value on wrap, exactly like core::pool.
            v.generation = 1;
        }
    }

    void audio::stop(audio_voice_id id)
    {
        if (voice* v = resolve(id))
        {
            free_voice(*v);
        }
    }

    void audio::set_paused(audio_voice_id id, bool paused)
    {
        if (voice* v = resolve(id))
        {
            v->paused = paused;
        }
    }

    bool audio::is_playing(audio_voice_id id) const
    {
        return resolve(id) != nullptr;
    }

    void audio::set_gain(audio_voice_id id, float gain)
    {
        if (voice* v = resolve(id))
        {
            v->gain = gain;
        }
    }

    void audio::set_pitch(audio_voice_id id, float pitch)
    {
        if (voice* v = resolve(id))
        {
            v->pitch = pitch > 0.0f ? pitch : 1.0f;
        }
    }

    void audio::set_pan(audio_voice_id id, float pan)
    {
        if (voice* v = resolve(id))
        {
            v->pan = pan;
        }
    }

    void audio::set_position(audio_voice_id id, const core::math::vec3& position)
    {
        if (voice* v = resolve(id))
        {
            v->position = position;
        }
    }

    void audio::set_attenuation(audio_voice_id id, float min_distance, float max_distance, float rolloff)
    {
        if (voice* v = resolve(id))
        {
            v->min_distance = std::max(min_distance, 0.0f);
            v->max_distance = std::max(max_distance, v->min_distance + 1e-3f);
            v->rolloff = std::max(rolloff, 0.0f);
        }
    }

    audio::listener_token audio::attach_listener()
    {
        const listener_token token = m_next_listener_token++;
        listener_record record;
        record.token = token;
        m_listeners.push_back(record);
        return token;
    }

    void audio::detach_listener(listener_token token)
    {
        auto it = std::find_if(
            m_listeners.begin(), m_listeners.end(), [token](const listener_record& r) { return r.token == token; });
        if (it != m_listeners.end())
        {
            m_listeners.erase(it);
        }
    }

    void audio::set_listener_enabled(listener_token token, bool enabled)
    {
        for (listener_record& r : m_listeners)
        {
            if (r.token == token)
            {
                r.enabled = enabled;
                return;
            }
        }
    }

    void
    audio::set_listener_transform(listener_token token, const core::math::vec3& position, const core::math::vec3& right)
    {
        for (listener_record& r : m_listeners)
        {
            if (r.token == token)
            {
                r.position = position;
                r.right = right;
                return;
            }
        }
    }

    const audio::listener_record* audio::active_listener() const noexcept
    {
        // "Most recently attached wins": walk back to front and take the
        // first enabled one, mirroring the camera registry's arbitration in
        // spirit (rendering_engine/camera/camera_registry.cpp) without its
        // priority ranking.
        for (auto it = m_listeners.rbegin(); it != m_listeners.rend(); ++it)
        {
            if (it->enabled)
            {
                return &*it;
            }
        }
        return nullptr;
    }

    void audio::mix_frames(float* out, std::size_t frame_count)
    {
        std::fill(out, out + frame_count * k_mixer_channels, 0.0f);
        const listener_record* listener = active_listener();

        for (voice& v : m_voices)
        {
            if (!v.active || v.paused || !v.clip)
            {
                continue;
            }

            float left_gain = 0.0f;
            float right_gain = 0.0f;
            if (v.spatial)
            {
                // No listener: keep the voice advancing (see the class docs
                // on update) but contribute nothing audible.
                if (listener != nullptr)
                {
                    const core::math::vec3 offset = v.position - listener->position;
                    const float distance = core::math::length(offset);

                    float pan = 0.0f;
                    if (distance > degenerate_length)
                    {
                        pan = std::clamp(core::math::dot(offset / distance, listener->right), -1.0f, 1.0f);
                    }

                    const float span = std::max(v.max_distance - v.min_distance, 1e-4f);
                    const float t = std::clamp((distance - v.min_distance) / span, 0.0f, 1.0f);
                    const float attenuation = std::pow(1.0f - t, v.rolloff);
                    pan_gains(v.gain * attenuation, pan, left_gain, right_gain);
                }
            }
            else
            {
                pan_gains(v.gain, v.pan, left_gain, right_gain);
            }

            const std::vector<float>& samples = v.clip->samples;
            const std::size_t clip_frames = v.clip->frame_count;

            for (std::size_t i = 0; i < frame_count; ++i)
            {
                const auto base = static_cast<std::size_t>(v.cursor);
                if (base >= clip_frames)
                {
                    break;
                }
                const float frac = static_cast<float>(v.cursor - static_cast<double>(base));
                const std::size_t next = base + 1 < clip_frames ? base + 1 : (v.loop ? 0 : base);

                const float l = samples[base * 2 + 0] + (samples[next * 2 + 0] - samples[base * 2 + 0]) * frac;
                const float r = samples[base * 2 + 1] + (samples[next * 2 + 1] - samples[base * 2 + 1]) * frac;

                out[i * 2 + 0] += l * left_gain;
                out[i * 2 + 1] += r * right_gain;

                v.cursor += static_cast<double>(v.pitch);
                if (v.cursor >= static_cast<double>(clip_frames))
                {
                    if (v.loop)
                    {
                        v.cursor = std::fmod(v.cursor, static_cast<double>(clip_frames));
                    }
                    else
                    {
                        free_voice(v);
                        break;
                    }
                }
            }
        }

        // A hard clip guards against many overlapping voices summing past
        // full scale; a limiter/compressor is out of scope (see class docs).
        for (std::size_t i = 0; i < frame_count * k_mixer_channels; ++i)
        {
            out[i] = std::clamp(out[i], -1.0f, 1.0f);
        }
    }

    void audio::update(float delta_time)
    {
        // ~100 ms of buffered audio absorbs ordinary frame-time jitter
        // without adding noticeable latency; a stall (a debugger pause, a
        // minimized window returning) is capped so one catch-up tick never
        // mixes more than half a second in one call.
        constexpr std::size_t k_target_buffered_frames = k_mixer_sample_rate / 10;
        constexpr std::size_t k_max_frames_per_update = k_mixer_sample_rate / 2;

        std::size_t frames_needed = 0;
        if (m_available)
        {
            const int queued_bytes = SDL_GetAudioStreamQueued(m_stream);
            const std::size_t queued_frames =
                queued_bytes > 0 ? static_cast<std::size_t>(queued_bytes) / (k_mixer_channels * sizeof(float)) : 0;
            frames_needed = queued_frames < k_target_buffered_frames ? k_target_buffered_frames - queued_frames : 0;
        }
        else
        {
            // No device to keep topped up: advance voices by wall-clock time
            // instead, so is_playing()/looping bookkeeping stays correct.
            frames_needed =
                static_cast<std::size_t>(std::clamp(delta_time, 0.0f, 1.0f) * static_cast<float>(k_mixer_sample_rate));
        }
        frames_needed = std::min(frames_needed, k_max_frames_per_update);
        if (frames_needed == 0)
        {
            return;
        }

        m_scratch.assign(frames_needed * k_mixer_channels, 0.0f);
        mix_frames(m_scratch.data(), frames_needed);

        if (m_available)
        {
            SDL_PutAudioStreamData(m_stream, m_scratch.data(), static_cast<int>(m_scratch.size() * sizeof(float)));
        }
    }
} // namespace core
